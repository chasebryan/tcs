#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TCS_TEST_IPC_ROUTER
#define init policy_init
#define notified policy_notified
#define protected policy_handler
#include "../servers/policy.c"
#undef init
#undef notified
#undef protected
#define init storage_init
#define notified storage_notified
#define protected storage_handler
#include "../servers/storage.c"
#undef init
#undef notified
#undef protected
#define init client_init
#define notified client_notified
#define protected client_handler
#include "../servers/client.c"
#undef init
#undef notified
#undef protected

enum caller { CLIENT, STORAGE, POLICY };
static enum caller current;
static unsigned calls, audit_calls;
static bool audit_ok = true;

static microkit_msginfo test_ppcall(microkit_channel ch, microkit_msginfo msg)
{
    enum caller previous = current;
    microkit_msginfo reply;
    ++calls;
    if (current == CLIENT) {
        assert(ch == 1);
        current = STORAGE;
        reply = storage_handler(0, msg);
    } else if (current == STORAGE) {
        assert(ch == 1);
        current = POLICY;
        reply = policy_handler(1, msg);
    } else {
        assert(ch == 2 && tcs_message(msg, TCS_AUDIT_APPEND, 7));
        ++audit_calls;
        reply = tcs_reply((struct tcs_result){audit_ok ? TCS_OK : TCS_AUDIT_FULL, 0});
    }
    current = previous;
    return reply;
}

static struct tcs_snapshot query(void)
{
    struct tcs_policy before = live;
    unsigned before_audit = audit_calls, before_calls = calls;
    current = CLIENT;
    struct tcs_snapshot s = tcs_snapshot_response(client_handler(0,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_STATUS), 0)));
    assert(s.status == TCS_OK);
    assert(calls == before_calls + 2 && audit_calls == before_audit);
    assert(memcmp(&before, &live, sizeof live) == 0);
    return s;
}

static void control(unsigned op)
{
    current = POLICY;
    microkit_mr_set(0, TCS_CLIENT_SUBJECT);
    microkit_mr_set(1, op == TCS_GRANT ? TCS_OBJECT : 0);
    microkit_mr_set(2, op == TCS_GRANT ? TCS_READ : 0);
    microkit_mr_set(3, 0);
    struct tcs_result r = tcs_response(policy_handler(0,
        microkit_msginfo_new(TCS_LABEL(op), 4)));
    assert(r.status == (audit_ok ? TCS_OK : TCS_AUDIT_FULL));
}

static void rejected_requests(void)
{
    typedef microkit_msginfo (*handler)(microkit_channel, microkit_msginfo);
    handler handlers[] = {client_handler, storage_handler, policy_handler};
    unsigned ops[] = {TCS_CLIENT_STATUS, TCS_STORE_STATUS, TCS_SELF_STATUS};
    struct tcs_policy before = live;
    unsigned before_audit = audit_calls, before_calls = calls;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned channel = i == 2 ? 1 : 0;
        for (unsigned count = 1; count <= 64; ++count) {
            microkit_mr_set(0, 2); /* Attempt to select another subject. */
            struct tcs_result r = tcs_response(handlers[i](channel,
                microkit_msginfo_new(TCS_LABEL(ops[i]), count)));
            assert(r.status == TCS_BAD_MESSAGE && r.value == 0);
        }
        struct tcs_result r = tcs_response(handlers[i](9,
            microkit_msginfo_new(TCS_LABEL(ops[i]), 0)));
        assert(r.status == TCS_DENIED && r.value == 0);
        r = tcs_response(handlers[i](channel, microkit_msginfo_new(0x200 | ops[i], 0)));
        assert(r.status == TCS_BAD_MESSAGE && r.value == 0);
    }
    struct tcs_result admin = tcs_response(policy_handler(0,
        microkit_msginfo_new(TCS_LABEL(TCS_SELF_STATUS), 0)));
    assert(admin.status == TCS_DENIED && admin.value == 0);
    assert(audit_calls == before_audit && calls == before_calls);
    assert(memcmp(&before, &live, sizeof live) == 0);
}

static void rejected_replies(void)
{
    struct tcs_snapshot bad = tcs_snapshot_response(tcs_reply((struct tcs_result){TCS_OK, 0}));
    assert(bad.status == TCS_BAD_MESSAGE && bad.generation == 0);
    struct tcs_snapshot valid = {TCS_OK, TCS_ACTIVE, UINT64_MAX, TCS_OBJECT, TCS_READ};
    microkit_msginfo reply = tcs_snapshot_reply(valid);
    assert(tcs_snapshot_response(reply).generation == UINT64_MAX);
    for (unsigned word = 0; word < 4; ++word) {
        reply = tcs_snapshot_reply(valid);
        microkit_mr_set(word, word == 1 ? 0 : UINT64_MAX);
        bad = tcs_snapshot_response(reply);
        assert(bad.status == TCS_BAD_MESSAGE && bad.state == 0 && bad.generation == 0 &&
               bad.object == 0 && bad.rights == 0);
    }
    for (unsigned count = 0; count <= 64; ++count) {
        if (count == 4) continue;
        bad = tcs_snapshot_response(microkit_msginfo_new(TCS_SNAPSHOT_REPLY, count));
        assert(bad.status == TCS_BAD_MESSAGE && bad.generation == 0);
    }
    bad = tcs_snapshot_response(tcs_reply((struct tcs_result){TCS_DENIED, UINT64_MAX}));
    assert(bad.status == TCS_BAD_MESSAGE && bad.object == 0);
    bad = tcs_snapshot_response(tcs_reply((struct tcs_result){UINT64_MAX, 0}));
    assert(bad.status == TCS_BAD_MESSAGE);
    valid.state = TCS_RESTRICTED;
    bad = tcs_snapshot_response(tcs_snapshot_reply(valid));
    assert(bad.status == TCS_BAD_MESSAGE && bad.rights == 0);
}

int main(void)
{
    current = CLIENT;
    struct tcs_snapshot s = tcs_snapshot_response(client_handler(0,
        microkit_msginfo_new(TCS_LABEL(TCS_CLIENT_STATUS), 0)));
    assert(s.status == TCS_DENIED); /* Policy is not initialized. */
    policy_init();
    live.subjects[2] = (struct tcs_subject){999, TCS_OBJECT, TCS_READ, TCS_ACTIVE};
    microkit_mr_set(0, 2); /* Stale registers are not part of a zero-word request. */
    s = query();
    assert(s.state == TCS_RESTRICTED && s.generation == 0 && s.rights == 0 && s.object == 0);
    rejected_requests();
    control(TCS_GRANT);
    s = query();
    assert(s.state == TCS_ACTIVE && s.generation == 1 && s.object == TCS_OBJECT && s.rights == TCS_READ);
    control(TCS_REVOKE);
    s = query();
    assert(s.state == TCS_RESTRICTED && s.generation == 2 && s.rights == 0 && s.object == 0);
    control(TCS_GRANT); control(TCS_QUARANTINE);
    s = query();
    assert(s.state == TCS_QUARANTINED && s.generation == 4 && s.rights == 0 && s.object == 0);
    control(TCS_RESTORE);
    s = query();
    assert(s.state == TCS_RESTRICTED && s.generation == 5);
    audit_ok = false;
    control(TCS_GRANT); /* Failure must not appear as a live grant. */
    assert(query().state == TCS_RESTRICTED && query().generation == 5);
    control(TCS_QUARANTINE); /* Reductions still commit with no audit. */
    s = query();
    assert(s.state == TCS_QUARANTINED && s.generation == 6 && s.rights == 0);
    assert(live.subjects[2].generation == 999 && live.subjects[2].state == TCS_ACTIVE);
    rejected_replies();
    puts("TCS SELF-STATUS IPC TESTS PASS (host adapters; actual kernel path tested in QEMU)");
}
