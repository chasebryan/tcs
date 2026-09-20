/* Test-only fixture provider. Firmware data never comes from the serial path. */
#include "tcs/admin_ipc.h"
uintptr_t fwcfg_base_vaddr;
static bool fixture_ready;
static uint8_t boot_context[TCS_BOOT_BYTES], packets[TCS_TEST_ADMIN_COMMANDS][TCS_ADMIN_PACKET_BYTES];
static bool select_item(void *context, uint16_t key)
{
    (void)context;
    *(volatile uint16_t *)(fwcfg_base_vaddr + 8) = (uint16_t)(key << 8 | key >> 8);
    __asm__ volatile("dmb osh" ::: "memory"); return true;
}
static bool read_byte(void *context, uint8_t *byte)
{
    (void)context; *byte = *(volatile uint8_t *)fwcfg_base_vaddr;
    __asm__ volatile("dmb osh" ::: "memory"); return true;
}
void init(void)
{
    struct tcs_fwcfg_io io = {0, select_item, read_byte}; struct tcs_boot_context decoded;
    if (tcs_fwcfg_read(io, TCS_BOOT_ITEM, boot_context, sizeof boot_context) != TCS_BOOT_OK ||
        tcs_boot_decode_test(boot_context, sizeof boot_context, &decoded) != TCS_BOOT_OK) return;
    for (unsigned i = 0; i < TCS_TEST_ADMIN_COMMANDS; ++i) {
        char name[] = "opt/tcs/admin-00";
        name[sizeof name - 3] = (char)('0' + (i+1) / 10);
        name[sizeof name - 2] = (char)('0' + (i+1) % 10);
        if (tcs_fwcfg_read(io, name, packets[i], sizeof packets[i]) != TCS_BOOT_OK) return;
    }
    fixture_ready = true;
}
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (!fixture_ready) return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (ch == 0 && microkit_msginfo_get_label(msg) == TCS_TEST_BOOT_CONTEXT && !microkit_msginfo_get_count(msg)) {
        tcs_words_from_bytes(boot_context, sizeof boot_context);
        return microkit_msginfo_new(TCS_TEST_BOOT_DATA, sizeof boot_context / 8);
    }
    if (ch == 1 && microkit_msginfo_get_label(msg) == TCS_TEST_BOOT_PACKET && microkit_msginfo_get_count(msg) == 1) {
        uint64_t index = microkit_mr_get(0);
        if (index < TCS_TEST_ADMIN_COMMANDS) {
            tcs_words_from_bytes(packets[index], sizeof packets[index]);
            return microkit_msginfo_new(TCS_TEST_BOOT_DATA, TCS_ADMIN_PACKET_WORDS);
        }
    }
    return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
}
