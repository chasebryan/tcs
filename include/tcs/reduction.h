#ifndef TCS_REDUCTION_H
#define TCS_REDUCTION_H

#include "tcs/lifecycle.h"
#include <stdatomic.h>

/* Experimental, native-tested reduction latch. Not connected to a guest.
 * Private single-owner state; zero-initialize ONCE with the lifecycle bootstrap
 * preconditions. No reset, slot reuse, shared model memory or kernel operations.
 * Only the mailbox word is shared. Its writer has reduction authority over BOTH
 * slots, never reserve/activate/stop-confirmation/drain authority. */
#define TCS_RD_ALL UINT64_C(3)
struct tcs_reduction {
    struct tcs_lifecycle life;
    uint64_t inhibited, input_fault;
};
struct tcs_reduction_mailbox { _Atomic(uint64_t) requests; };
enum tcs_rd_poll_status { TCS_RD_OK, TCS_RD_MALFORMED, TCS_RD_INVALID_STATE };

bool tcs_rd_valid(const struct tcs_reduction *control);
/* One copied atomic sample, not a mutable payload. Unknown bits permanently
 * inhibit BOTH slots and latch input_fault; MALFORMED thus commits reductions.
 * Invalid private state is unchanged; the adapter must stop/fail closed. */
enum tcs_rd_poll_status tcs_rd_poll(struct tcs_reduction *control, uint64_t sample);
/* Poll FIRST, even if the subsequent event is denied/invalid. Event failure
 * does not roll back separately observed reductions. Caller/approval come from
 * trusted adapter state, not from the mailbox. Never bypass with tcs_lc_apply. */
struct tcs_lc_result tcs_rd_apply(struct tcs_reduction *control, uint64_t sample,
    enum tcs_lc_actor actor, struct tcs_lc_event event, bool audit_ok);
/* Stop intent only, never proof that a kernel operation has completed. Invalid
 * private state conservatively requests both stops, with no model repair. */
uint64_t tcs_rd_stop_mask(const struct tcs_reduction *control);
/* Additional gate only; ordinary policy checks remain required. */
bool tcs_rd_allows(const struct tcs_reduction *control, uint64_t slot, uint64_t incarnation);

/* Native C11 mailbox experiment; no notification, scheduling or cross-PD memory
 * mapping is implemented. Initialize before publication, never clear it.
 * Publish is atomic OR (lock-free is required, wait-freedom is NOT claimed).
 * Success means published, NOT observed/contained/stopped/acknowledged.
 * A future adapter samples before every model event and on independent wakeups.
 * The shared cell cannot preserve a hostile write withdrawn before any sample. */
bool tcs_rd_publish(struct tcs_reduction_mailbox *mailbox, uint64_t requests);
uint64_t tcs_rd_sample(const struct tcs_reduction_mailbox *mailbox);

#endif
