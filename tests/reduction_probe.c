/* Native independent-state test transport; NOT a wire ABI or persisted loader. */
#include "tcs/reduction.h"
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    for (;;) {
        struct tcs_reduction c = {0};
        int count = scanf("%" SCNu64, &c.life.last_incarnation);
        if (count == EOF) return ferror(stdin) ? 1 : 0;
        if (count != 1) return 1;
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
            struct tcs_lc_slot *s = &c.life.slots[i];
            if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                &s->state, &s->incarnation, &s->issued, &s->pending, &s->stopped, &s->drained) != 6) return 1;
        }
        uint64_t sample; unsigned kind, actor, audit;
        struct tcs_lc_event e;
        if (scanf("%" SCNu64 " %" SCNu64 " %u %" SCNu64 " %u %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %u",
            &c.inhibited, &c.input_fault, &kind, &sample, &actor, &e.op, &e.slot, &e.incarnation, &e.sequence, &audit) != 10 ||
            kind > 1 || actor > 99 || audit > 1) return 1;
        struct tcs_lc_result r = kind ? tcs_rd_apply(&c, sample, (enum tcs_lc_actor)actor, e, audit != 0) :
            (struct tcs_lc_result){tcs_rd_poll(&c, sample), 0};
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64, r.status, r.value, c.life.last_incarnation);
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
            const struct tcs_lc_slot *s = &c.life.slots[i];
            printf(" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
                s->state, s->incarnation, s->issued, s->pending, s->stopped, s->drained);
        }
        printf(" %" PRIu64 " %" PRIu64 " %" PRIu64 " %u %u\n", c.inhibited, c.input_fault,
            tcs_rd_stop_mask(&c), tcs_rd_allows(&c, 0, c.life.slots[0].incarnation),
            tcs_rd_allows(&c, 1, c.life.slots[1].incarnation));
    }
}
