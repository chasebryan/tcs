/* Native test transport only: NOT a clock source, wire ABI or snapshot loader. */
#include "tcs/deadline.h"
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    for (;;) {
        struct tcs_deadline c = {0};
        int count = scanf("%" SCNu64, &c.control.life.last_incarnation);
        if (count == EOF) return ferror(stdin) ? 1 : 0;
        if (count != 1) return 1;
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
            struct tcs_lc_slot *s = &c.control.life.slots[i];
            if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                &s->state, &s->incarnation, &s->issued, &s->pending, &s->stopped, &s->drained) != 6) return 1;
        }
        if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
            &c.control.inhibited, &c.control.input_fault, &c.last_tick, &c.clock_fault,
            &c.end[0], &c.end[1]) != 6) return 1;
        uint64_t sample, tick, duration; unsigned kind, ok, actor, audit;
        struct tcs_lc_event e;
        if (scanf("%u %" SCNu64 " %u %" SCNu64 " %" SCNu64 " %u %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %u",
            &kind, &sample, &ok, &tick, &duration, &actor, &e.op, &e.slot, &e.incarnation,
            &e.sequence, &audit) != 11 || kind > 1 || ok > 1 || actor > 99 || audit > 1) return 1;
        struct tcs_dl_reading reading = {tick, ok != 0};
        struct tcs_lc_result r = kind ? tcs_dl_apply(&c, sample, reading, (enum tcs_lc_actor)actor, e, audit != 0, duration) :
            (struct tcs_lc_result){tcs_dl_poll(&c, sample, reading), 0};
        if (!tcs_dl_valid(&c)) return 2;
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64, r.status, r.value, c.control.life.last_incarnation);
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
            const struct tcs_lc_slot *s = &c.control.life.slots[i];
            printf(" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
                s->state, s->incarnation, s->issued, s->pending, s->stopped, s->drained);
        }
        printf(" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %u %u\n",
            c.control.inhibited, c.control.input_fault, c.last_tick, c.clock_fault, c.end[0], c.end[1],
            tcs_dl_stop_mask(&c), tcs_dl_allows(&c, 0, c.control.life.slots[0].incarnation),
            tcs_dl_allows(&c, 1, c.control.life.slots[1].incarnation));
    }
}
