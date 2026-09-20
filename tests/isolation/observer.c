/* Test-only parent reuses the real terminal's bounded UART output and commands.
 * No child supervision capability is added to the ordinary terminal image. */
#define init terminal_init
#define notified terminal_notified
#include "../../servers/terminal.c"
#undef init
#undef notified
#include "cases.h"
#if !defined(TCS_RELEASE_PROFILE) && !defined(TCS_HOST_TEST)
#error "Isolation observer is release-kernel test machinery only"
#endif
_Static_assert(ISO_VM_FAULT == seL4_Fault_VMFault, "SDK fault tag changed");
_Static_assert(ISO_FAULT_WORDS == seL4_VMFault_Length, "SDK fault shape changed");
uintptr_t isolation_canary_vaddr;
static unsigned completed;
static enum { START, WAIT, NEXT, FINISH, TERMINAL, FAILED } stage;

static bool intact(void)
{
    if (*(volatile uint64_t *)isolation_canary_vaddr != ISO_CANARY)
        return false;
    struct tcs_snapshot s = tcs_snapshot_response(microkit_ppcall(CLIENT_CHANNEL,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_STATUS), 0)));
    return s.status == TCS_OK && s.state == TCS_RESTRICTED && s.generation == 0 &&
           s.object == 0 && s.rights == 0;
}

static void progress(void)
{
    for (unsigned step = 0; step < 4; ++step) {
        if (!flush() || failed)
            return;
        if (stage == START || (stage == NEXT && completed < ISO_CASES)) {
            stage = WAIT;
            microkit_pd_resume(completed + 1);
            return;
        }
        if (stage == NEXT) {
            append("TCS ISOLATION PASS cases=6 canary=intact policy=restricted\r\n");
            stage = FINISH;
        } else if (stage == FINISH) {
            stage = TERMINAL;
            terminal_init();
            return;
        } else {
            return;
        }
    }
}

void init(void)
{
    /* Parent priority exceeds every probe: stop them before their init runs. */
    for (unsigned child = 1; child <= ISO_CASES; ++child)
        microkit_pd_stop(child);
    if (!intact()) {
        append("TCS ISOLATION FAIL initial state\r\n");
        stage = FAILED;
    } else {
        append("TCS ISOLATION BEGIN release-kernel cases=6\r\n");
        stage = START;
    }
    progress();
}

void notified(microkit_channel ch)
{
    if (stage == TERMINAL)
        terminal_notified(ch);
    else if (ch == SERIAL_CHANNEL)
        progress();
}

seL4_Bool fault(microkit_child child, microkit_msginfo msg, microkit_msginfo *reply)
{
    (void)reply;
    struct iso_fault f = {microkit_msginfo_get_label(msg), microkit_msginfo_get_count(msg), 0, 0, 0, 0};
    if (f.count == ISO_FAULT_WORDS) {
        f.ip = microkit_mr_get(seL4_VMFault_IP);
        f.address = microkit_mr_get(seL4_VMFault_Addr);
        f.instruction = microkit_mr_get(seL4_VMFault_PrefetchFault);
        f.fsr = microkit_mr_get(seL4_VMFault_FSR);
    }
    /* Capture kernel-supplied words before any nested IPC overwrites MRs. */
    bool valid = stage == WAIT && child == completed + 1 &&
                 iso_fault_matches(child, f) && intact();
    append(valid ? "FAULT PASS child=" : "TCS ISOLATION FAIL child="); decimal(child);
    append(" label="); decimal(f.label); append(" words="); decimal(f.count);
    append(" ip="); decimal(f.ip); append(" address="); decimal(f.address);
    append(" instruction="); decimal(f.instruction); append(" fsr="); decimal(f.fsr);
    append("\r\n");
    if (valid) { ++completed; stage = NEXT; }
    else stage = FAILED;
    progress();
    /* Never resume/reply to the faulting child. This is not restart/recovery. */
    return seL4_False;
}
