#include "tcs/ipc.h"

static bool failed;

static void expect(const char *name, struct tcs_result r, uint64_t status)
{
    bool ok = r.status == status && (status == TCS_OK || r.value == 0);
    microkit_dbg_puts(ok ? "PASS " : "FAIL ");
    microkit_dbg_puts(name);
    microkit_dbg_puts("\n");
    failed |= !ok;
}

static struct tcs_result control(uint64_t op, uint64_t subject)
{
    return tcs_policy_call(0, (struct tcs_request){op, subject, 0, 0, 0});
}

static struct tcs_result grant(uint64_t subject)
{
    return tcs_policy_call(0, (struct tcs_request){TCS_GRANT, subject,
        TCS_OBJECT, TCS_READ, 0});
}

static struct tcs_result read_object(uint64_t object, uint64_t rights, uint64_t generation)
{
    microkit_mr_set(0, object);
    microkit_mr_set(1, rights);
    microkit_mr_set(2, generation);
    return tcs_response(microkit_ppcall(1,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_READ), 3)));
}

void init(void)
{
    microkit_dbg_puts("\nTCS 0.1.0 Seed | seL4 / Microkit | AArch64\n");
    expect("default deny", read_object(TCS_OBJECT, TCS_READ, 1), TCS_DENIED);
    struct tcs_result session = grant(1);
    expect("admin grants session", session, TCS_OK);
    uint64_t old = session.value;
    struct tcs_result data = read_object(TCS_OBJECT, TCS_READ, old);
    expect("authorized IPC read", data, TCS_OK);
    if (data.value != TCS_SAMPLE) { failed = true; microkit_dbg_puts("FAIL fixture bytes\n"); }
    expect("wrong object denied", read_object(43, TCS_READ, old), TCS_DENIED);
    expect("write escalation denied", read_object(TCS_OBJECT, TCS_WRITE, old), TCS_DENIED);
    expect("revoke acknowledged", control(TCS_REVOKE, 1), TCS_OK);
    expect("revoked session denied", read_object(TCS_OBJECT, TCS_READ, old), TCS_DENIED);
    session = grant(1);
    expect("explicit regrant", session, TCS_OK);
    expect("old session remains stale", read_object(TCS_OBJECT, TCS_READ, old), TCS_STALE);
    expect("new session works", read_object(TCS_OBJECT, TCS_READ, session.value), TCS_OK);
    expect("quarantine acknowledged", control(TCS_QUARANTINE, 1), TCS_OK);
    expect("quarantine blocks reads", read_object(TCS_OBJECT, TCS_READ, session.value), TCS_ISOLATED);
    expect("quarantine blocks grants", grant(1), TCS_ISOLATED);
    expect("explicit restore", control(TCS_RESTORE, 1), TCS_OK);
    expect("restore grants no access", read_object(TCS_OBJECT, TCS_READ, session.value), TCS_DENIED);
    expect("malformed IPC denied", tcs_response(microkit_ppcall(1,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_BAD_LENGTH), 0))), TCS_BAD_MESSAGE);
    expect("client cannot grant through storage", tcs_response(microkit_ppcall(1,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_TRY_GRANT), 0))), TCS_BAD_MESSAGE);
    struct tcs_result stranger = grant(2);
    expect("grant to different subject", stranger, TCS_OK);
    expect("other subject handle denied", read_object(TCS_OBJECT, TCS_READ, stranger.value), TCS_DENIED);

    /* Exercise finite audit capacity and its security failure mode on the real IPC path. */
    session = grant(1);
    expect("grant before audit exhaustion", session, TCS_OK);
    struct tcs_result logged = tcs_response(microkit_ppcall(2,
        microkit_msginfo_new(TCS_LABEL(TCS_AUDIT_COUNT), 0)));
    expect("audit count available", logged, TCS_OK);
    if (logged.value == 0 || logged.value > TCS_AUDIT_CAPACITY) failed = true;
    microkit_mr_set(0, 0);
    microkit_msginfo record = microkit_ppcall(2,
        microkit_msginfo_new(TCS_LABEL(TCS_AUDIT_GET), 1));
    bool record_ok = tcs_message(record, TCS_AUDIT_GET, 8) &&
        microkit_mr_get(0) == 1 && microkit_mr_get(1) == TCS_CHECK &&
        microkit_mr_get(2) == TCS_STORAGE && microkit_mr_get(3) == 1 &&
        microkit_mr_get(7) == TCS_DENIED;
    microkit_dbg_puts(record_ok ? "PASS audit record readback\n" : "FAIL audit record readback\n");
    failed |= !record_ok;
    for (uint64_t i = logged.value; i < TCS_AUDIT_CAPACITY; ++i)
        expect("bounded audit append", read_object(TCS_OBJECT, TCS_READ, session.value), TCS_OK);
    expect("full audit denies reads", read_object(TCS_OBJECT, TCS_READ, session.value), TCS_AUDIT_FULL);
    expect("full audit denies grants", grant(1), TCS_AUDIT_FULL);
    expect("revoke reports missing audit", control(TCS_REVOKE, 1), TCS_AUDIT_FULL);
    microkit_dbg_puts(failed ? "TCS SEED FAIL\n" : "TCS SEED PASS\n");
}

void notified(microkit_channel ch) { (void)ch; }
