/* First integration is test-boot-only; no operator provisioning is implied. */
#include "tcs/admin_server.h"
static struct tcs_admin administrator;

void init(void)
{
    microkit_msginfo reply = microkit_ppcall(1, microkit_msginfo_new(TCS_TEST_BOOT_CONTEXT, 0));
    if (microkit_msginfo_get_label(reply) != TCS_TEST_BOOT_DATA ||
        microkit_msginfo_get_count(reply) != TCS_BOOT_BYTES / 8) return;
    uint8_t bytes[TCS_BOOT_BYTES]; struct tcs_boot_context context;
    tcs_bytes_from_words(bytes, sizeof bytes);
    if (tcs_boot_decode_test(bytes, sizeof bytes, &context) != TCS_BOOT_OK) return;
    (void)tcs_admin_init(&administrator, context.public_key, context.realm, context.boot);
}
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{
    return tcs_admin_server_request(&administrator, ch, msg);
}
