#ifndef TCS_ADMIN_H
#define TCS_ADMIN_H
#include <stddef.h>
#include "tcs/policy.h"

#define TCS_ADMIN_MESSAGE_BYTES 128u
#define TCS_ADMIN_PACKET_BYTES 192u
#define TCS_ADMIN_ID_BYTES 32u

/* Local API statuses, not a stable IPC ABI or an execution receipt. */
enum tcs_admin_status { TCS_ADMIN_ACCEPTED, TCS_ADMIN_NOT_READY,
    TCS_ADMIN_BAD_PACKET, TCS_ADMIN_BAD_CONTEXT, TCS_ADMIN_BAD_SIGNATURE,
    TCS_ADMIN_REPLAY, TCS_ADMIN_BUSY, TCS_ADMIN_EXHAUSTED,
    TCS_ADMIN_BAD_COMPLETION };
struct tcs_admin_command {
    uint64_t sequence, expected_generation;
    struct tcs_request request;
};
struct tcs_admin {
    uint8_t public_key[32], realm[32], boot[32];
    uint64_t next_sequence, pending_sequence;
    bool initialized, exhausted;
};

/* Trusted setup only, on a zero-initialized instance. No reset/rekey RPC.
 * Key must be a correctly generated, independently authorized Ed25519 key.
 * Realm and fresh boot identity must come from trusted provisioning, not input.
 * Nonzero checks do NOT establish key validity, entropy, uniqueness or trust. */
bool tcs_admin_init(struct tcs_admin *admin, const uint8_t public_key[32],
    const uint8_t realm[32], const uint8_t boot[32]);

/* Encode the exact public signed message. Never serializes C struct padding. */
bool tcs_admin_encode(uint8_t message[TCS_ADMIN_MESSAGE_BYTES],
    const uint8_t realm[32], const uint8_t boot[32], struct tcs_admin_command command);

/* Single-threaded owner only; packet is an immutable private snapshot, not
 * concurrently writable shared memory. Buffers/outputs/state must not alias.
 * Admission consumes the sequence BEFORE forwarding; it does not mutate policy.
 * One command may be pending. Uncertain completion stays pending, fail closed. */
enum tcs_admin_status tcs_admin_admit(struct tcs_admin *admin,
    const uint8_t *packet, size_t length, struct tcs_admin_command *command);
/* Call only after a validated policy execution/denial receipt, never a timeout
 * or an untrusted terminal acknowledgement. Does not rewind the sequence. */
enum tcs_admin_status tcs_admin_complete(struct tcs_admin *admin, uint64_t sequence);

/* Policy-owner-side precondition, evaluated atomically with candidate mutation.
 * Run on a candidate and use the existing audit/commit step afterward.
 * A remote snapshot check in an admin server is NOT a substitute. */
struct tcs_result tcs_policy_admin_compare_apply(struct tcs_policy *candidate,
    enum tcs_actor actor, struct tcs_admin_command command);
#endif
