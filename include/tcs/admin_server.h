#ifndef TCS_ADMIN_SERVER_H
#define TCS_ADMIN_SERVER_H
#include "tcs/admin_ipc.h"
/* Shared request boundary for test-boot and explicitly provisioned instances. */
static inline microkit_msginfo tcs_admin_server_request(struct tcs_admin *administrator,
    microkit_channel ch, microkit_msginfo msg)
{
    if (ch != 0) return tcs_reply((struct tcs_result){TCS_DENIED, 0});
    if (microkit_msginfo_get_label(msg) != TCS_ADMIN_SUBMIT ||
        microkit_msginfo_get_count(msg) != TCS_ADMIN_PACKET_WORDS)
        return tcs_admin_admission_reply(TCS_ADMIN_BAD_PACKET);
    uint8_t packet[TCS_ADMIN_PACKET_BYTES]; struct tcs_admin_command command;
    tcs_bytes_from_words(packet, sizeof packet); /* Private snapshot before nested IPC. */
    enum tcs_admin_status status = tcs_admin_admit(administrator, packet, sizeof packet, &command);
    if (status != TCS_ADMIN_ACCEPTED) return tcs_admin_admission_reply(status);
    tcs_execution_words(command);
    microkit_msginfo reply = microkit_ppcall(2, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6));
    struct tcs_admin_receipt receipt;
    if (!tcs_admin_receipt_decode(reply, &receipt) || !tcs_admin_receipt_valid(command, receipt))
        return tcs_admin_admission_reply(TCS_ADMIN_BAD_COMPLETION);
    if (tcs_admin_complete(administrator, command.sequence) != TCS_ADMIN_ACCEPTED)
        return tcs_admin_admission_reply(TCS_ADMIN_BAD_COMPLETION);
    return tcs_admin_receipt_reply(receipt);
}
#endif
