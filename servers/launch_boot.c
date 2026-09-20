/* Exclusive firmware owner; no terminal channel, fixture packets, or secrets. */
#include "tcs/admin_ipc.h"
#include "tcs/launch_profile.h"
uintptr_t fwcfg_base_vaddr;
static uint8_t bytes[TCS_LAUNCH_BYTES];
static bool ready;
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
    struct tcs_launch launch; struct tcs_fwcfg_io io = {0, select_item, read_byte};
    ready = tcs_fwcfg_read(io, TCS_LAUNCH_ITEM, bytes, sizeof bytes) == TCS_BOOT_OK &&
        tcs_launch_decode(bytes, sizeof bytes, TCS_LAUNCH_MODE, &launch);
}
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    if (!ready || ch != 0) return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (microkit_msginfo_get_label(msg) != TCS_LAUNCH_CONTEXT || microkit_msginfo_get_count(msg))
        return tcs_reply((struct tcs_result){TCS_BAD_MESSAGE, 0});
    tcs_words_from_bytes(bytes, sizeof bytes);
    return microkit_msginfo_new(TCS_LAUNCH_DATA, sizeof bytes / 8);
}
