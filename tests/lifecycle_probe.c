/* Native test transport only. Each line supplies an independent model state;
 * this is not a guest protocol or an interface for loading persisted state. */
#include <inttypes.h>
#include <stdio.h>
#include "tcs/lifecycle.h"

int main(void)
{
    for (;;) {
        struct tcs_lifecycle p = {0};
        int count = scanf("%" SCNu64, &p.last_incarnation);
        if (count == EOF) return ferror(stdin) ? 1 : 0;
        if (count != 1) return 1;
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
            struct tcs_lc_slot *s = &p.slots[i];
            if (scanf("%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                &s->state, &s->incarnation, &s->issued, &s->pending, &s->stopped, &s->drained) != 6) return 1;
        }
        unsigned actor, audit;
        struct tcs_lc_event e;
        if (scanf("%u %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %u",
            &actor, &e.op, &e.slot, &e.incarnation, &e.sequence, &audit) != 6 || actor > 99 || audit > 1) return 1;
        struct tcs_lc_result r = tcs_lc_apply(&p, (enum tcs_lc_actor)actor, e, audit != 0);
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64, r.status, r.value, p.last_incarnation);
        for (unsigned i = 0; i < TCS_LC_SLOTS; ++i) {
            const struct tcs_lc_slot *s = &p.slots[i];
            printf(" %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
                s->state, s->incarnation, s->issued, s->pending, s->stopped, s->drained);
        }
        if (putchar('\n') == EOF) return 1;
    }
}
