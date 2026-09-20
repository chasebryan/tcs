#ifndef TCS_ADMIN_RECEIPT_H
#define TCS_ADMIN_RECEIPT_H
#include "tcs/admin.h"

/* A receipt from the private policy channel, not a signed durable attestation. */
struct tcs_admin_receipt {
    struct tcs_admin_command command;
    uint64_t decision, audit_ok, applied;
    struct tcs_snapshot post;
};
bool tcs_admin_execution_valid(struct tcs_admin_command command);
bool tcs_admin_receipt_valid(struct tcs_admin_command pending,
    struct tcs_admin_receipt receipt);
#endif
