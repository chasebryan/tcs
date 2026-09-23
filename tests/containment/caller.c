#include "runtime.h"
uintptr_t caller_phase_vaddr;
static unsigned cl_phase;
void init(void) { }
void notified(microkit_channel ch)
{
    if (ch != 0) return;
    if (cl_phase == 0) {
        struct tcs_lc_result r = ct_response(microkit_ppcall(1, microkit_msginfo_new(CT_KICK, 0)));
        cl_phase = (r.status == TCS_LC_OK && r.value == 1) ? 1 : 99;
        ct_store(caller_phase_vaddr, cl_phase);
    } else if (cl_phase == 1) {
        cl_phase = 2; ct_store(caller_phase_vaddr, cl_phase);
        (void)microkit_ppcall(1, microkit_msginfo_new(CT_HANG, 0));
        cl_phase = 3; ct_store(caller_phase_vaddr, cl_phase); /* Any return is a test failure. */
    }
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo m)
{ (void)ch; (void)m; return ct_reply(TCS_LC_DENIED, 0); }
