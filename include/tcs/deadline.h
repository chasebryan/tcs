#ifndef TCS_DEADLINE_H
#define TCS_DEADLINE_H

#include "tcs/reduction.h"

/* NATIVE-ONLY experiment: fixed, nonrenewable lifetime from successful reserve.
 * Private single owner, zero once with the lifecycle bootstrap preconditions.
 * No clock reader, timer, notification, scheduler or kernel operation is here.
 * Never bypass this wrapper through its embedded reduction/lifecycle state. */
struct tcs_deadline {
    struct tcs_reduction control;
    uint64_t last_tick, clock_fault;
    uint64_t end[TCS_LC_SLOTS];
};
/* Supplied ONLY by a trusted adapter's fresh, coherent read of its fixed clock.
 * Neither field may come from worker/terminal telemetry or notification counts.
 * ok includes source/epoch/unit validation; it is not authenticated by this API. */
struct tcs_dl_reading { uint64_t tick; bool ok; };
enum tcs_dl_status { TCS_DL_OK, TCS_DL_FAULT, TCS_DL_INVALID_STATE };

bool tcs_dl_valid(const struct tcs_deadline *owner);
/* Expire at tick >= end. Regression/read failure sticks and inhibits BOTH slots.
 * Repeated equal ticks are allowed (not proof the clock is progressing).
 * FAULT commits reductions and remains sticky; invalid private state is unchanged.
 * Every event and independent wakeup must sample; silence has no guarantee. */
enum tcs_dl_status tcs_dl_poll(struct tcs_deadline *owner, uint64_t requests,
    struct tcs_dl_reading reading);
/* Poll FIRST, including for rejected events. Duration must be positive and fit
 * for RESERVE, and zero for every other event. Only successful audited reserve
 * sets end = sampled tick + duration; no cancellation, extension or renewal.
 * Caller/audit/duration are trusted admission inputs, not clock/request payloads. */
struct tcs_lc_result tcs_dl_apply(struct tcs_deadline *owner, uint64_t requests,
    struct tcs_dl_reading reading, enum tcs_lc_actor actor,
    struct tcs_lc_event event, bool audit_ok, uint64_t duration);
/* Snapshots relative to last poll, not current-time checks or physical evidence.
 * A resource adapter must poll before admission and still enforce normal policy. */
uint64_t tcs_dl_stop_mask(const struct tcs_deadline *owner);
bool tcs_dl_allows(const struct tcs_deadline *owner, uint64_t slot, uint64_t incarnation);

#endif
