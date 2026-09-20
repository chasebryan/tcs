/* First integration is test-boot-only; no operator provisioning is implied. */
#include "tcs/admin_ipc.h"
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
    if (ch != 0) return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (microkit_msginfo_get_label(msg) != TCS_ADMIN_SUBMIT ||
        microkit_msginfo_get_count(msg) != TCS_ADMIN_PACKET_WORDS)
        return tcs_admin_admission_reply(TCS_ADMIN_BAD_PACKET);
    uint8_t packet[TCS_ADMIN_PACKET_BYTES]; struct tcs_admin_command command;
    /* Capture every word before crypto or nested calls; no shared input page. */
    tcs_bytes_from_words(packet, sizeof packet);
    enum tcs_admin_status status = tcs_admin_admit(&administrator, packet, sizeof packet, &command);
    if (status != TCS_ADMIN_ACCEPTED) return tcs_admin_admission_reply(status);
    tcs_execution_words(command);
    microkit_msginfo reply = microkit_ppcall(2, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6));
    struct tcs_admin_receipt receipt;
    if (!tcs_admin_receipt_decode(reply, &receipt) || !tcs_admin_receipt_valid(command, receipt))
        return tcs_admin_admission_reply(TCS_ADMIN_BAD_COMPLETION); /* Stays pending. */
    if (tcs_admin_complete(&administrator, command.sequence) != TCS_ADMIN_ACCEPTED)
        return tcs_admin_admission_reply(TCS_ADMIN_BAD_COMPLETION);
    return tcs_admin_receipt_reply(receipt);
}
