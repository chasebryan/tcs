#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TCS_TEST_IPC_ROUTER
#define init admin_server_init
#define notified admin_server_notified
#define protected admin_server_handler
#include "../servers/admin.c"
#undef init
#undef notified
#undef protected
#define init policy_server_init
#define notified policy_server_notified
#define protected policy_server_handler
#include "../servers/admin_policy.c"
#undef init
#undef notified
#undef protected
#include "monocypher-ed25519.h"

static uint8_t context[112] = {'T','C','S','-','B','O','O','T',1,1}, secret[64];
static unsigned current, executions, audits, audit_failure;
static int corrupt_word = -1;
static bool corrupt_shape, interleave, bad_boot;
static struct tcs_admin_command grant = {1, 0, {TCS_GRANT, 1, TCS_OBJECT, TCS_READ, 0}};

static microkit_msginfo test_ppcall(microkit_channel channel, microkit_msginfo message)
{
    if (current == 0 && channel == 1) {
        assert(message.label == TCS_TEST_BOOT_CONTEXT && message.count == 0);
        tcs_words_from_bytes(context, sizeof context);
        return microkit_msginfo_new(TCS_TEST_BOOT_DATA, bad_boot ? 13 : 14);
    }
    if (current == 0) {
        assert(channel == 2 && message.label == TCS_ADMIN_EXECUTE && message.count == 6);
        ++executions;
        if (interleave) {
            assert(tcs_policy_apply(&policy_live, TCS_ADMIN,
                (struct tcs_request){TCS_QUARANTINE, 1, 0, 0, 0}).status == TCS_OK);
            interleave = false;
        }
        current = 1;
        microkit_msginfo reply = policy_server_handler(0, message);
        current = 0;
        if (corrupt_word >= 0) test_mrs[corrupt_word] ^= UINT64_MAX;
        if (corrupt_shape) reply.count = 12;
        return reply;
    }
    assert(channel == 2 && tcs_message(message, TCS_AUDIT_APPEND, 7));
    ++audits;
    /* All nested call registers are overwritten, including request identity. */
    for (unsigned i = 0; i < 64; ++i) test_mrs[i] = UINT64_C(0xbadc0ffee);
    if (audit_failure == 1) return tcs_reply((struct tcs_result){TCS_AUDIT_FULL, 0});
    if (audit_failure == 2) return microkit_msginfo_new(0x999, 2);
    if (audit_failure == 3) return microkit_msginfo_new(TCS_REPLY, 1);
    if (audit_failure == 4) return tcs_reply((struct tcs_result){TCS_OK, 0});
    if (audit_failure == 5) return tcs_reply((struct tcs_result){TCS_OK, TCS_AUDIT_CAPACITY + 1});
    return tcs_reply((struct tcs_result){TCS_OK, audits});
}
static void reset(void)
{
    administrator = (struct tcs_admin){0}; policy_live = (struct tcs_policy){0};
    policy_ready = execution_exhausted = false; execution_sequence = 1;
    current = executions = audits = audit_failure = 0;
    corrupt_word = -1; corrupt_shape = interleave = bad_boot = false;
    policy_server_init(); admin_server_init(); assert(administrator.initialized);
}
static microkit_msginfo submit(struct tcs_admin_command c)
{
    uint8_t packet[192]; assert(tcs_admin_encode(packet, context + 16, context + 48, c));
    crypto_ed25519_sign(packet + 128, secret, packet, 128);
    tcs_words_from_bytes(packet, sizeof packet); current = 0;
    return admin_server_handler(0, microkit_msginfo_new(TCS_ADMIN_SUBMIT, 24));
}
static struct tcs_admin_receipt receipt(struct tcs_admin_command c)
{
    struct tcs_admin_receipt r;
    assert(tcs_admin_receipt_decode(submit(c), &r) && tcs_admin_receipt_valid(c, r));
    assert(administrator.pending_sequence == 0);
    return r;
}
static void admission(microkit_msginfo message, unsigned status)
{
    assert(message.label == TCS_ADMIN_ADMISSION && message.count == 1 && test_mrs[0] == status);
}
static void malformed(void)
{
    reset();
    for (unsigned count = 0; count <= 64; ++count) if (count != 24)
        admission(admin_server_handler(0, microkit_msginfo_new(TCS_ADMIN_SUBMIT, count)), TCS_ADMIN_BAD_PACKET);
    admission(admin_server_handler(0, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 24)), TCS_ADMIN_BAD_PACKET);
    assert(tcs_response(admin_server_handler(3, microkit_msginfo_new(TCS_ADMIN_SUBMIT, 24))).status == TCS_DENIED);
    for (unsigned count = 0; count <= 64; ++count) if (count != 6)
        assert(tcs_response(policy_server_handler(0, microkit_msginfo_new(TCS_ADMIN_EXECUTE, count))).status == TCS_BAD_MESSAGE);
    for (unsigned op = TCS_GRANT; op <= TCS_RESTORE; ++op)
        assert(tcs_response(policy_server_handler(0, microkit_msginfo_new(TCS_LABEL(op), 4))).status == TCS_BAD_MESSAGE);
    assert(tcs_response(policy_server_handler(1, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6))).status == TCS_BAD_MESSAGE);
    assert(tcs_response(policy_server_handler(3, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6))).status == TCS_DENIED);
    assert(executions == 0 && audits == 0 && execution_sequence == 1 && administrator.next_sequence == 1);
    struct tcs_admin_command bad = grant; bad.request.subject = TCS_SUBJECTS;
    tcs_execution_words(bad);
    assert(tcs_response(policy_server_handler(0, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6))).status == TCS_BAD_MESSAGE);
    bad = grant; bad.sequence = 2; tcs_execution_words(bad);
    assert(tcs_response(policy_server_handler(0, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6))).status == TCS_STALE);
    assert(audits == 0 && execution_sequence == 1);
    administrator = (struct tcs_admin){0}; bad_boot = true; admin_server_init();
    admission(submit(grant), TCS_ADMIN_NOT_READY); assert(executions == 0);
    puts("PASS exact admin/policy channels, wire shapes, no legacy admin endpoint, boot failure");
}
static void transitions(void)
{
    reset(); struct tcs_admin_receipt r = receipt(grant);
    assert(r.decision == TCS_OK && r.audit_ok && r.applied && r.post.generation == 1);
    admission(submit(grant), TCS_ADMIN_REPLAY); assert(executions == 1 && audits == 1);
    struct tcs_admin_command c = grant; c.sequence = 2;
    r = receipt(c); assert(r.decision == TCS_STALE && !r.applied && r.post.generation == 1);
    c = (struct tcs_admin_command){3, 1, {TCS_REVOKE, 1, 0, 0, 0}};
    r = receipt(c); assert(r.applied && r.post.generation == 2 && r.post.state == TCS_RESTRICTED);
    c = (struct tcs_admin_command){4, 2, {TCS_RESTORE, 1, 0, 0, 0}};
    r = receipt(c); assert(r.decision == TCS_DENIED && !r.applied);
    reset(); interleave = true;
    r = receipt(grant); assert(r.decision == TCS_STALE && r.post.generation == 1 && r.post.state == TCS_QUARANTINED);
    c = grant; c.sequence = 2; c.expected_generation = 1;
    r = receipt(c); assert(r.decision == TCS_ISOLATED && !r.applied);
    for (unsigned failure = 1; failure <= 5; ++failure) {
        reset(); (void)receipt(grant); audit_failure = failure;
        c = (struct tcs_admin_command){2, 1, {TCS_REVOKE, 1, 0, 0, 0}};
        r = receipt(c); assert(!r.audit_ok && r.applied && r.post.generation == 2);
        c = grant; c.sequence = 3; c.expected_generation = 2;
        r = receipt(c); assert(r.decision == TCS_OK && !r.audit_ok && !r.applied && r.post.generation == 2);
        c = (struct tcs_admin_command){4, 2, {TCS_QUARANTINE, 1, 0, 0, 0}};
        r = receipt(c); assert(!r.audit_ok && r.applied && r.post.generation == 3);
        c = (struct tcs_admin_command){5, 3, {TCS_RESTORE, 1, 0, 0, 0}};
        r = receipt(c); assert(!r.audit_ok && !r.applied && r.post.state == TCS_QUARANTINED);
    }
    puts("PASS correlated receipts, nested-MR clobber, stale post-auth state, audit failures and authority reductions");
}
static void uncertain_completion(void)
{
    for (int word = -1; word < 13; ++word) {
        reset(); corrupt_word = word; corrupt_shape = word == -1;
        admission(submit(grant), TCS_ADMIN_BAD_COMPLETION);
        assert(administrator.pending_sequence == 1 && administrator.next_sequence == 2);
        assert(policy_live.subjects[1].generation == 1); /* Commit occurred, response was corrupted. */
        struct tcs_admin_command c = grant; c.sequence = 2; c.expected_generation = 1;
        admission(submit(c), TCS_ADMIN_BUSY); assert(executions == 1 && audits == 1);
        admission(admin_server_handler(0, microkit_msginfo_new(TCS_ADMIN_ADMISSION, 1)), TCS_ADMIN_BAD_PACKET);
        assert(administrator.pending_sequence == 1);
    }
    reset(); (void)receipt(grant);
    tcs_execution_words(grant); current = 1;
    assert(tcs_response(policy_server_handler(0, microkit_msginfo_new(TCS_ADMIN_EXECUTE, 6))).status == TCS_STALE);
    assert(audits == 1 && policy_live.subjects[1].generation == 1);
    puts("PASS corrupted receipts leave committed requests pending; no untrusted completion or duplicate policy execution");
}
static void limits(void)
{
    reset(); administrator.next_sequence = execution_sequence = UINT64_MAX;
    struct tcs_admin_command c = {UINT64_MAX, 0, {TCS_REVOKE, 1, 0, 0, 0}};
    assert(receipt(c).applied && administrator.exhausted && execution_exhausted);
    admission(submit(c), TCS_ADMIN_EXHAUSTED); assert(executions == 1);
    reset(); policy_live.subjects[1].generation = UINT64_MAX;
    c = grant; c.expected_generation = UINT64_MAX;
    assert(receipt(c).decision == TCS_EXHAUSTED);
    c = (struct tcs_admin_command){2, UINT64_MAX, {TCS_QUARANTINE, 1, 0, 0, 0}};
    assert(receipt(c).applied);
    c = (struct tcs_admin_command){3, UINT64_MAX, {TCS_RESTORE, 1, 0, 0, 0}};
    assert(receipt(c).applied);
    c = grant; c.sequence = 4; c.expected_generation = UINT64_MAX;
    assert(receipt(c).decision == TCS_EXHAUSTED);
    puts("PASS independent admin/policy sequence exhaustion and saturated generation receipts");
}
static void receipt_matrix(void)
{
    const uint64_t generations[] = {0, 1, UINT64_MAX-1, UINT64_MAX};
    const uint64_t operations[] = {TCS_GRANT, TCS_REVOKE, TCS_QUARANTINE, TCS_RESTORE};
    unsigned checked = 0;
    for (unsigned state = 0; state <= TCS_QUARANTINED; ++state)
    for (unsigned g = 0; g < 4; ++g)
    for (unsigned op = 0; op < 4; ++op)
    for (unsigned audit = 0; audit < 2; ++audit)
    for (unsigned stale = 0; stale < 2; ++stale) {
        if (state == TCS_ACTIVE && generations[g] == 0) continue;
        struct tcs_policy live = {0};
        live.subjects[1] = (struct tcs_subject){generations[g], state == TCS_ACTIVE ? TCS_OBJECT : 0,
            state == TCS_ACTIVE ? TCS_READ : 0, state};
        struct tcs_policy candidate = live;
        struct tcs_admin_command c = {1, generations[g] ^ stale,
            {operations[op], 1, operations[op] == TCS_GRANT ? TCS_OBJECT : 0,
             operations[op] == TCS_GRANT ? TCS_READ : 0, 0}};
        struct tcs_result decision = tcs_policy_admin_compare_apply(&candidate, TCS_ADMIN, c);
        (void)tcs_policy_commit(&live, &candidate, c.request, decision, audit);
        struct tcs_subject s = live.subjects[1];
        struct tcs_admin_receipt r = {c, decision.status, audit,
            decision.status == TCS_OK && (audit || operations[op] == TCS_REVOKE || operations[op] == TCS_QUARANTINE),
            {TCS_OK, s.state, s.generation, s.object, s.rights}};
        assert(tcs_admin_receipt_valid(c, r));
        r.applied ^= 1; assert(!tcs_admin_receipt_valid(c, r));
        ++checked;
    }
    assert(checked == 176);
    puts("PASS 176 valid state/generation/op/audit/precondition receipt combinations and inverted effects");
}
int main(void)
{
    uint8_t seed[32] = {0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60};
    context[16] = 0x54; context[48] = 0x42;
    crypto_ed25519_key_pair(secret, context + 80, seed);
    malformed(); transitions(); uncertain_completion(); limits(); receipt_matrix();
    crypto_wipe(secret, sizeof secret);
    puts("TCS ADMIN IPC TESTS PASS (real adapters, mocked kernel/audit transport)");
}
