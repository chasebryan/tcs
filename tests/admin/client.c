/* Scripted signed-IPC exercise, followed by the ordinary read-only terminal. */
#define init terminal_init
#define notified terminal_notified
#include "../../servers/terminal.c"
#undef init
#undef notified
#include "tcs/admin_ipc.h"
#include "scenario.h"

static unsigned command_index;
static bool running_terminal, test_failed, announcing;
static bool admission(microkit_msginfo m, unsigned code)
{
    return microkit_msginfo_get_label(m) == TCS_ADMIN_ADMISSION &&
        microkit_msginfo_get_count(m) == 1 && microkit_mr_get(0) == code;
}
static microkit_msginfo submit(const uint8_t packet[192])
{
    tcs_words_from_bytes(packet, 192);
    return microkit_ppcall(2, microkit_msginfo_new(TCS_ADMIN_SUBMIT, 24));
}
static struct tcs_result read_generation(uint64_t generation)
{
    microkit_mr_set(0, TCS_OBJECT); microkit_mr_set(1, TCS_READ); microkit_mr_set(2, generation);
    return tcs_response(microkit_ppcall(CLIENT_CHANNEL, microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_READ), 3)));
}
static bool exercise(unsigned index)
{
    microkit_mr_set(0, index);
    microkit_msginfo msg = microkit_ppcall(3, microkit_msginfo_new(TCS_TEST_BOOT_PACKET, 1));
    if (microkit_msginfo_get_label(msg) != TCS_TEST_BOOT_DATA || microkit_msginfo_get_count(msg) != 24) return false;
    uint8_t packet[192]; tcs_bytes_from_words(packet, sizeof packet);
    if (!index) {
        if (!admission(microkit_ppcall(2, microkit_msginfo_new(TCS_ADMIN_SUBMIT, 23)), TCS_ADMIN_BAD_PACKET)) return false;
        packet[191] ^= 1;
        bool denied = admission(submit(packet), TCS_ADMIN_BAD_SIGNATURE);
        packet[191] ^= 1;
        if (!denied) return false;
        struct tcs_result escalation = tcs_response(microkit_ppcall(CLIENT_CHANNEL,
            microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_TRY_GRANT), 0)));
        if (escalation.status == TCS_OK) return false;
    }
    struct tcs_admin_receipt r;
    if (!tcs_admin_receipt_decode(submit(packet), &r) || !tcs_admin_receipt_valid(scenario[index], r)) return false;
    const uint64_t generations[] = {1,1,2,3,4,4,5,6,7,7,8,8};
    const uint64_t states[] = {TCS_ACTIVE,TCS_ACTIVE,TCS_RESTRICTED,TCS_ACTIVE,TCS_QUARANTINED,TCS_QUARANTINED,
        TCS_RESTRICTED,TCS_ACTIVE,TCS_RESTRICTED,TCS_RESTRICTED,TCS_QUARANTINED,TCS_QUARANTINED};
    uint64_t decision = index == 1 ? TCS_STALE : index == 5 ? TCS_ISOLATED : TCS_OK;
    bool applied = index != 1 && index != 5 && index != 9 && index != 11;
    if (r.decision != decision || r.audit_ok != (index < 8) || r.applied != applied ||
        r.post.generation != generations[index] || r.post.state != states[index]) return false;
    struct tcs_snapshot actual = tcs_snapshot_response(microkit_ppcall(CLIENT_CHANNEL,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_STATUS), 0)));
    if (actual.status != TCS_OK || actual.state != r.post.state || actual.generation != r.post.generation ||
        actual.object != r.post.object || actual.rights != r.post.rights) return false;
    if (!index && !admission(submit(packet), TCS_ADMIN_REPLAY)) return false;
    uint64_t read_status = index >= 8 ? TCS_AUDIT_FULL : states[index] == TCS_ACTIVE ? TCS_OK :
        states[index] == TCS_QUARANTINED ? TCS_ISOLATED : TCS_DENIED;
    struct tcs_result read = read_generation(generations[index]);
    if (read.status != read_status || read.value != (read_status == TCS_OK ? TCS_SAMPLE : 0)) return false;
    if (index == 3 && read_generation(1).status != TCS_STALE) return false;
    if (index == 7) {
        bool full = false;
        for (unsigned i = 0; i <= TCS_AUDIT_CAPACITY; ++i) {
            struct tcs_result result = read_generation(6);
            if (result.status == TCS_AUDIT_FULL && result.value == 0) { full = true; break; }
            if (result.status != TCS_OK || result.value != TCS_SAMPLE) return false;
        }
        if (!full) return false;
    }
    append("ADMIN PASS seq="); decimal(index + 1);
    append(" decision="); decimal(r.decision); append(" audit="); decimal(r.audit_ok);
    append(" applied="); decimal(r.applied); append(" generation="); decimal(r.post.generation); append("\r\n");
    return true;
}
static void progress(void)
{
    for (unsigned step = 0; step < 2; ++step) {
        if (!flush() || failed || test_failed) return;
        if (command_index < TCS_TEST_ADMIN_COMMANDS) {
            if (!exercise(command_index)) {
                test_failed = true; append("TCS ADMIN IPC FAIL seq="); decimal(command_index + 1); append("\r\n");
                (void)flush(); return;
            }
            ++command_index;
        } else if (!announcing) {
            append("TCS ADMIN IPC PASS commands=12 state=quarantined generation=8\r\n"); announcing = true;
        } else {
            running_terminal = true; terminal_init(); return;
        }
    }
    (void)flush(); /* Serial progress/space notification continues the bounded script. */
}
void init(void) { append("TCS ADMIN IPC TEST ONLY\r\n"); progress(); }
void notified(microkit_channel ch)
{
    if (running_terminal) terminal_notified(ch);
    else if (ch == SERIAL_CHANNEL) progress();
}
