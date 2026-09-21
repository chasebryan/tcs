#ifndef TCS_LIFECYCLE_H
#define TCS_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

/* Experimental model only: not a kernel capability, wire ABI, or guest service.
 * Zero-initialize once, with two distinct unused/stopped domains as a trusted
 * precondition. Single-owner, serialized calls only; no concurrent-access API.
 * No reset, slot reuse, or restart operation is provided. */
#define TCS_LC_SLOTS 2u
enum tcs_lc_state { TCS_LC_UNUSED, TCS_LC_STARTING, TCS_LC_READY,
    TCS_LC_ACTIVE, TCS_LC_CLOSING, TCS_LC_RETIRED };
enum tcs_lc_actor { TCS_LC_UNKNOWN, TCS_LC_ADMIN, TCS_LC_SUPERVISOR,
    TCS_LC_BROKER, TCS_LC_DETECTOR, TCS_LC_WORKER };
enum tcs_lc_op { TCS_LC_RESERVE = 1, TCS_LC_STARTED, TCS_LC_ACTIVATE,
    TCS_LC_BEGIN, TCS_LC_COMPLETE, TCS_LC_CONTAIN, TCS_LC_STOPPED,
    TCS_LC_DRAINED };
enum tcs_lc_status { TCS_LC_OK, TCS_LC_INVALID, TCS_LC_DENIED,
    TCS_LC_STALE, TCS_LC_BAD_STATE, TCS_LC_AUDIT_REQUIRED, TCS_LC_EXHAUSTED };

struct tcs_lc_slot {
    uint64_t state, incarnation, issued, pending, stopped, drained;
};
struct tcs_lifecycle {
    uint64_t last_incarnation;
    struct tcs_lc_slot slots[TCS_LC_SLOTS];
};
struct tcs_lc_event { uint64_t op, slot, incarnation, sequence; };
struct tcs_lc_result { uint64_t status, value; };

/* Structural validation does not authenticate state or prevent rollback. */
bool tcs_lc_valid(const struct tcs_lifecycle *life);
/* An additional work gate, never sufficient authorization to use a resource. */
bool tcs_lc_allows(const struct tcs_lifecycle *life, uint64_t slot, uint64_t incarnation);
/* Actor/audit evidence comes from a future trusted adapter, NOT event payload.
 * OK describes a model transition, not confirmation of a kernel operation.
 * Stop/drain confirmations bind slot+incarnation+issued-at-containment.
 * All failures return value zero and leave state byte-for-byte unchanged. */
struct tcs_lc_result tcs_lc_apply(struct tcs_lifecycle *life,
    enum tcs_lc_actor actor, struct tcs_lc_event event, bool audit_ok);

#endif
