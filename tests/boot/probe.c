/* Test-only bootstrap/crypto probe: no policy endpoint, no runtime reset API. */
#include "tcs/ipc.h"
#include "tcs/serial.h"
#include "tcs/boot.h"
#include "tcs/admin.h"

uintptr_t fwcfg_base_vaddr;
static char output[320];
static size_t length, sent;
static bool failed;
static void append(const char *s) { while (*s && length < sizeof output) output[length++] = *s++; }
static void hex(const uint8_t *bytes, size_t count)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) { char text[] = {digits[bytes[i] >> 4], digits[bytes[i] & 15], 0}; append(text); }
}
static void status(const char *label, unsigned value)
{
    append(label); char code[] = {(char)('0' + value), '\r', '\n', 0}; append(code);
}
static void flush(void)
{
    if (failed) return;
    /* Each callback has a fixed maximum; partial serial writes resume on notify. */
    for (unsigned attempt = 0; sent < length && attempt < 16; ++attempt) {
        size_t n = length - sent; if (n > TCS_SERIAL_CHUNK) n = TCS_SERIAL_CHUNK;
        for (size_t i = 0; i < n; ++i) microkit_mr_set(i, (uint8_t)output[sent+i]);
        struct tcs_result r = tcs_response(microkit_ppcall(0, microkit_msginfo_new(TCS_LABEL(TCS_SERIAL_WRITE), n)));
        if (r.status != TCS_OK || r.value > n) { failed = true; return; }
        sent += r.value;
        if (r.value < n) return;
    }
}
static bool select_item(void *context, uint16_t key)
{
    (void)context;
    *(volatile uint16_t *)(fwcfg_base_vaddr + 8) = (uint16_t)(key << 8 | key >> 8);
    __asm__ volatile("dmb osh" ::: "memory");
    return true;
}
static bool read_byte(void *context, uint8_t *byte)
{
    (void)context;
    *byte = *(volatile uint8_t *)fwcfg_base_vaddr;
    __asm__ volatile("dmb osh" ::: "memory");
    return true;
}
void init(void)
{
    struct tcs_fwcfg_io io = {0, select_item, read_byte};
    uint8_t bytes[TCS_BOOT_BYTES], packet[TCS_ADMIN_PACKET_BYTES];
    struct tcs_boot_context context;
    enum tcs_boot_status s = tcs_fwcfg_read(io, TCS_BOOT_ITEM, bytes, sizeof bytes);
    if (s == TCS_BOOT_OK) s = tcs_boot_decode_test(bytes, sizeof bytes, &context);
    append("TCS BOOT TEST ONLY\r\n"); status("BOOT status=", s);
    if (s == TCS_BOOT_OK) {
        append("BOOT nonce="); hex(context.boot, 32); append("\r\n");
        append("BOOT key="); hex(context.public_key, 32); append("\r\n");
        s = tcs_fwcfg_read(io, TCS_BOOT_COMMAND_ITEM, packet, sizeof packet);
        status("COMMAND transport=", s);
        if (s == TCS_BOOT_OK) {
            struct tcs_admin admin = {0}; struct tcs_admin_command command;
            if (!tcs_admin_init(&admin, context.public_key, context.realm, context.boot))
                append("FAIL admin initialization\r\n");
            else {
                enum tcs_admin_status a = tcs_admin_admit(&admin, packet, sizeof packet, &command);
                status("COMMAND admission=", a);
                if (a == TCS_ADMIN_ACCEPTED) {
                    status("COMMAND duplicate=", tcs_admin_admit(&admin, packet, sizeof packet, &command));
                    /* Test receipt only: this image has NO policy service. */
                    if (tcs_admin_complete(&admin, 1) != TCS_ADMIN_ACCEPTED) append("FAIL completion\r\n");
                    status("COMMAND replay=", tcs_admin_admit(&admin, packet, sizeof packet, &command));
                }
            }
        }
    }
    append("TCS BOOT TEST DONE\r\n"); flush();
}
void notified(microkit_channel channel) { if (channel == 0) flush(); }
microkit_msginfo protected(microkit_channel channel, microkit_msginfo message)
{
    (void)channel; (void)message; return tcs_reply((struct tcs_result){TCS_DENIED, 0});
}
