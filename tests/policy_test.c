#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/policy.h"

static struct tcs_request req(uint64_t op, uint64_t subject, uint64_t gen)
{
    bool access = op == TCS_GRANT || op == TCS_CHECK;
    return (struct tcs_request){op, subject, access ? TCS_OBJECT : 0,
        access ? TCS_READ : 0, gen};
}

static struct tcs_result step(struct tcs_policy *p, enum tcs_actor actor,
    struct tcs_request r, bool audit_ok)
{
    struct tcs_policy candidate = *p;
    struct tcs_result decision = tcs_policy_apply(&candidate, actor, r);
    return tcs_policy_commit(p, &candidate, r, decision, audit_ok);
}

static void lifecycle(void)
{
    struct tcs_policy p = {0};
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, 0), true).status == TCS_DENIED);
    struct tcs_result grant = step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true);
    assert(grant.status == TCS_OK && grant.value != 0);
    uint64_t old = grant.value;
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, old), true).status == TCS_OK);
    assert(step(&p, TCS_ADMIN, req(TCS_REVOKE, 1, 0), true).status == TCS_OK);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, old), true).status != TCS_OK);
    grant = step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true);
    assert(grant.value > old);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, old), true).status == TCS_STALE);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, grant.value), true).status == TCS_OK);
    assert(step(&p, TCS_ADMIN, req(TCS_QUARANTINE, 1, 0), true).status == TCS_OK);
    assert(step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true).status == TCS_ISOLATED);
    assert(step(&p, TCS_ADMIN, req(TCS_RESTORE, 1, 0), true).status == TCS_OK);
    assert(p.subjects[1].state == TCS_RESTRICTED && p.subjects[1].rights == 0);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, grant.value), true).status != TCS_OK);
    puts("PASS grant/revoke/regrant/quarantine/restore lifecycle");
}

static void authority(void)
{
    struct tcs_policy p = {0};
    for (unsigned op = TCS_GRANT; op <= TCS_RESTORE; ++op) {
        assert(step(&p, TCS_UNKNOWN, req(op, 1, 0), true).status == TCS_DENIED);
        if (op != TCS_CHECK)
            assert(step(&p, TCS_STORAGE, req(op, 1, 0), true).status == TCS_DENIED);
    }
    assert(step(&p, TCS_ADMIN, req(TCS_CHECK, 1, 0), true).status == TCS_DENIED);
    struct tcs_result g = step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 2, g.value), true).status == TCS_DENIED);
    struct tcs_request r = req(TCS_CHECK, 1, g.value);
    r.rights = TCS_WRITE;
    assert(step(&p, TCS_STORAGE, r, true).status == TCS_DENIED);
    r.rights = TCS_READ | TCS_WRITE;
    assert(step(&p, TCS_STORAGE, r, true).status == TCS_DENIED);
    r.rights = 0;
    assert(step(&p, TCS_STORAGE, r, true).status == TCS_DENIED);
    r.rights = TCS_READ;
    r.object++;
    assert(step(&p, TCS_STORAGE, r, true).status == TCS_DENIED);
    puts("PASS actor, subject, object, and rights boundaries");
}

static void malformed(void)
{
    struct tcs_policy p = {0}, original = p;
    assert(step(&p, TCS_ADMIN, req(0, 1, 0), true).status == TCS_BAD_MESSAGE);
    assert(step(&p, TCS_ADMIN, req(999, 1, 0), true).status == TCS_BAD_MESSAGE);
    assert(step(&p, TCS_ADMIN, req(TCS_GRANT, UINT64_MAX, 0), true).status == TCS_BAD_MESSAGE);
    assert(step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 12), true).status == TCS_BAD_MESSAGE);
    struct tcs_request r = req(TCS_REVOKE, 1, 0);
    r.object = TCS_OBJECT;
    assert(step(&p, TCS_ADMIN, r, true).status == TCS_BAD_MESSAGE);
    assert(memcmp(&original, &p, sizeof p) == 0);
    puts("PASS malformed commands leave policy unchanged");
}

static void audit_failure(void)
{
    struct tcs_policy p = {0};
    assert(step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), false).status == TCS_AUDIT_FULL);
    assert(p.subjects[1].rights == 0);
    struct tcs_result g = step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, g.value), false).status == TCS_AUDIT_FULL);
    assert(step(&p, TCS_ADMIN, req(TCS_REVOKE, 1, 0), false).status == TCS_AUDIT_FULL);
    assert(p.subjects[1].rights == 0);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, g.value), true).status != TCS_OK);
    step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true);
    step(&p, TCS_ADMIN, req(TCS_QUARANTINE, 1, 0), false);
    assert(p.subjects[1].state == TCS_QUARANTINED);
    step(&p, TCS_ADMIN, req(TCS_RESTORE, 1, 0), false);
    assert(p.subjects[1].state == TCS_QUARANTINED);
    puts("PASS audit failure blocks access but permits authority reduction");
}

static void exhaustion(void)
{
    struct tcs_policy p = {0};
    p.subjects[1].generation = UINT64_MAX - 1;
    struct tcs_result g = step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true);
    assert(g.status == TCS_OK && g.value == UINT64_MAX);
    step(&p, TCS_ADMIN, req(TCS_REVOKE, 1, 0), true);
    assert(p.subjects[1].generation == UINT64_MAX && p.subjects[1].rights == 0);
    assert(step(&p, TCS_ADMIN, req(TCS_GRANT, 1, 0), true).status == TCS_EXHAUSTED);
    assert(step(&p, TCS_STORAGE, req(TCS_CHECK, 1, g.value), true).status != TCS_OK);
    puts("PASS generation exhaustion cannot resurrect an old session");
}

static void transition_sequences(void)
{
    struct tcs_policy p = {0};
    uint64_t random = 0x43494e4958;
    for (unsigned i = 0; i < 50000; ++i) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        uint64_t subject = (random >> 8) % TCS_SUBJECTS;
        uint64_t op = 1 + random % 5;
        enum tcs_actor actor = (enum tcs_actor)((random >> 20) % 3);
        bool audit_ok = ((random >> 24) & 3) != 0;
        struct tcs_policy before = p;
        uint64_t generation = op == TCS_CHECK ? p.subjects[subject].generation : 0;
        struct tcs_result r = step(&p, actor, req(op, subject, generation), audit_ok);
        for (unsigned s = 0; s < TCS_SUBJECTS; ++s) {
            assert(p.subjects[s].generation >= before.subjects[s].generation);
            if (s != subject)
                assert(memcmp(&p.subjects[s], &before.subjects[s], sizeof p.subjects[s]) == 0);
            if (p.subjects[s].state != TCS_ACTIVE)
                assert(p.subjects[s].rights == 0);
        }
        if (op == TCS_CHECK && r.status == TCS_OK) {
            assert(actor == TCS_STORAGE && audit_ok);
            assert(before.subjects[subject].state == TCS_ACTIVE);
            assert(before.subjects[subject].rights == TCS_READ && generation != 0);
        }
        if (before.subjects[subject].rights == 0 && p.subjects[subject].rights != 0)
            assert(op == TCS_GRANT && actor == TCS_ADMIN && audit_ok);
    }
    puts("PASS 50000 deterministic transition steps preserve invariants");
}

static void self_status(void)
{
    struct tcs_policy p = {0};
    p.subjects[2] = (struct tcs_subject){999, TCS_OBJECT, TCS_READ, TCS_ACTIVE};
    struct tcs_policy before = p;
    for (unsigned actor = 0; actor < 5; ++actor) {
        struct tcs_snapshot s = tcs_policy_self_status(&p, (enum tcs_actor)actor);
        assert(s.status == (actor == TCS_STORAGE ? TCS_OK : TCS_DENIED));
        assert(s.state == TCS_RESTRICTED && s.generation == 0 && s.object == 0 && s.rights == 0);
        assert(memcmp(&p, &before, sizeof p) == 0);
    }
    p.subjects[TCS_CLIENT_SUBJECT].generation = UINT64_MAX;
    assert(tcs_policy_self_status(&p, TCS_STORAGE).generation == UINT64_MAX);
    p.subjects[TCS_CLIENT_SUBJECT].rights = TCS_READ; /* Inconsistent restricted state. */
    struct tcs_snapshot s = tcs_policy_self_status(&p, TCS_STORAGE);
    assert(s.status == TCS_BAD_MESSAGE && s.generation == 0 && s.rights == 0);
    puts("PASS self-status is read-only, fixed-subject, and rejects inconsistent metadata");
}

int main(void)
{
    lifecycle(); authority(); malformed(); audit_failure(); exhaustion(); transition_sequences(); self_status();
    puts("TCS POLICY TESTS PASS");
    return 0;
}
