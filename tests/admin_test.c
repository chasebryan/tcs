#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/admin.h"
#include "monocypher-ed25519.h"

/* PUBLIC TEST FIXTURES ONLY. Never provision these as an operator identity. */
static uint8_t secret[64], key[32], realm[32] = {0x52}, boot[32] = {0x42};
static struct tcs_admin_command grant = {1, 0, {TCS_GRANT, 1, TCS_OBJECT, TCS_READ, 0}};

static unsigned nibble(char c)
{
    return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10);
}
static size_t unhex(uint8_t *out, const char *hex)
{
    size_t n = strlen(hex) / 2;
    assert(strlen(hex) == n * 2);
    for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)((nibble(hex[2*i]) << 4) | nibble(hex[2*i+1]));
    return n;
}
static void rfc8032(void)
{
    /* RFC 8032 section 7.1, TEST 1/2/3; public algorithm test data. */
    const char *vectors[][4] = {
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
         "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
         "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
        {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
         "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
         "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
    };
    for (size_t i = 0; i < 3; ++i) {
        uint8_t seed[32], expected_key[32], expected_sig[64], sig[64], message[2] = {0};
        assert(unhex(seed, vectors[i][0]) == 32);
        assert(unhex(expected_key, vectors[i][1]) == 32);
        size_t length = unhex(message, vectors[i][2]);
        assert(unhex(expected_sig, vectors[i][3]) == 64);
        crypto_ed25519_key_pair(secret, key, seed);
        assert(memcmp(key, expected_key, 32) == 0);
        crypto_ed25519_sign(sig, secret, message, length);
        assert(memcmp(sig, expected_sig, 64) == 0);
        assert(crypto_ed25519_check(expected_sig, key, message, length) == 0);
        sig[0] ^= 1; assert(crypto_ed25519_check(sig, key, message, length) != 0);
    }
    puts("PASS RFC 8032 Ed25519 vectors 1, 2, 3 and corrupted signatures");
}
static struct tcs_admin fresh(void)
{
    struct tcs_admin a = {0}; assert(tcs_admin_init(&a, key, realm, boot)); return a;
}
static void sign_packet(uint8_t packet[192], struct tcs_admin_command c)
{
    assert(tcs_admin_encode(packet, realm, boot, c));
    crypto_ed25519_sign(packet + 128, secret, packet, 128);
}
static void unchanged_rejection(struct tcs_admin *a, const uint8_t *p, size_t length)
{
    struct tcs_admin before = *a;
    struct tcs_admin_command out = grant;
    assert(tcs_admin_admit(a, p, length, &out) != TCS_ADMIN_ACCEPTED);
    assert(memcmp(&before, a, sizeof before) == 0);
    assert(out.sequence == 0 && out.expected_generation == 0 && out.request.op == 0 &&
           out.request.subject == 0 && out.request.object == 0 && out.request.rights == 0 &&
           out.request.generation == 0);
}
static void encoding_and_tampering(void)
{
    uint8_t packet[193] = {0}; sign_packet(packet, grant);
    const uint8_t prefix[16] = {'T','C','S','-','A','D','M','I','N','-',1,0,0,0,0,0};
    assert(memcmp(packet, prefix, 16) == 0 && memcmp(packet + 16, realm, 32) == 0 &&
           memcmp(packet + 48, boot, 32) == 0);
    assert(packet[80] == 1 && packet[88] == TCS_GRANT && packet[96] == 1 &&
           packet[104] == 42 && packet[112] == 1 && packet[120] == 0);
    for (size_t offset = 80; offset < 128; offset += 8)
        for (size_t j = 1; j < 8; ++j) assert(packet[offset+j] == 0);
    struct tcs_admin a = fresh();
    for (size_t length = 0; length <= 193; ++length)
        if (length != 192) unchanged_rejection(&a, packet, length);
    unchanged_rejection(&a, NULL, 192);
    for (size_t i = 0; i < 192; ++i) for (unsigned bit = 0; bit < 8; ++bit) {
        packet[i] ^= (uint8_t)(1u << bit);
        unchanged_rejection(&a, packet, 192);
        packet[i] ^= (uint8_t)(1u << bit);
    }
    uint8_t unaligned[193]; memcpy(unaligned + 1, packet, 192);
    struct tcs_admin_command out;
    assert(tcs_admin_admit(&a, unaligned + 1, 192, &out) == TCS_ADMIN_ACCEPTED);
    assert(out.sequence == 1 && out.request.subject == 1 && out.request.generation == 0);
    puts("PASS exact encoding/length, unaligned input, 1536 single-bit packet corruptions, zero error output");
}
static void replay_and_context(void)
{
    uint8_t p[192]; sign_packet(p, grant);
    struct tcs_admin a = fresh(); struct tcs_admin_command out;
    assert(tcs_admin_admit(&a, p, 192, &out) == TCS_ADMIN_ACCEPTED);
    assert(a.pending_sequence == 1 && a.next_sequence == 2);
    assert(tcs_admin_admit(&a, p, 192, &out) == TCS_ADMIN_BUSY);
    assert(tcs_admin_complete(&a, 2) == TCS_ADMIN_BAD_COMPLETION && a.pending_sequence == 1);
    assert(tcs_admin_complete(&a, 0) == TCS_ADMIN_BAD_COMPLETION);
    assert(!tcs_admin_init(&a, key, realm, boot)); /* No replay-counter reset. */
    assert(tcs_admin_complete(&a, 1) == TCS_ADMIN_ACCEPTED);
    assert(tcs_admin_complete(&a, 1) == TCS_ADMIN_BAD_COMPLETION);
    assert(tcs_admin_admit(&a, p, 192, &out) == TCS_ADMIN_REPLAY);
    /* Explicit limitation: rebuilding volatile state with the SAME boot ID
     * re-enables old signatures. Fresh trusted boot IDs are a deployment gate. */
    struct tcs_admin reused_boot = fresh();
    assert(tcs_admin_admit(&reused_boot, p, 192, &out) == TCS_ADMIN_ACCEPTED);
    for (unsigned variation = 0; variation < 3; ++variation) {
        a = fresh();
        if (variation == 0) a.realm[0] ^= 1;
        if (variation == 1) a.boot[0] ^= 1;
        if (variation == 2) a.public_key[0] ^= 1;
        unchanged_rejection(&a, p, 192);
    }
    /* Also reject a DIFFERENT, correctly signed boot/realm/sequence. */
    for (unsigned offset = 16; offset <= 80; offset += 32) {
        sign_packet(p, grant); p[offset] ^= 2;
        crypto_ed25519_sign(p + 128, secret, p, 128);
        a = fresh(); unchanged_rejection(&a, p, 192);
    }
    a = fresh(); a.next_sequence = UINT64_MAX; /* Direct boundary fixture, no runtime reset API. */
    struct tcs_admin_command last = grant; last.sequence = UINT64_MAX;
    sign_packet(p, last);
    assert(tcs_admin_admit(&a, p, 192, &out) == TCS_ADMIN_ACCEPTED);
    assert(a.exhausted && a.next_sequence == UINT64_MAX);
    assert(tcs_admin_complete(&a, UINT64_MAX) == TCS_ADMIN_ACCEPTED);
    assert(tcs_admin_admit(&a, p, 192, &out) == TCS_ADMIN_EXHAUSTED);
    puts("PASS boot/realm/key binding, replay, pending receipt, reset rejection, sequence exhaustion");
}
static void malformed_signed_commands(void)
{
    uint8_t p[192]; struct tcs_admin a = fresh();
    const unsigned offsets[] = {10, 11, 15, 80, 88, 96, 104, 112};
    const uint8_t values[] = {2, 1, 1, 0, TCS_CHECK, TCS_SUBJECTS, 0, TCS_WRITE};
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        sign_packet(p, grant); p[offsets[i]] = values[i];
        crypto_ed25519_sign(p + 128, secret, p, 128);
        unchanged_rejection(&a, p, 192);
    }
    for (uint64_t op = TCS_REVOKE; op <= TCS_RESTORE; ++op) {
        struct tcs_admin_command c = {1, 0, {op, 1, 0, 0, 0}};
        sign_packet(p, c); p[104] = 42;
        crypto_ed25519_sign(p + 128, secret, p, 128); unchanged_rejection(&a, p, 192);
    }
    struct tcs_admin zero = {0}; struct tcs_admin_command out;
    assert(tcs_admin_admit(&zero, p, 192, &out) == TCS_ADMIN_NOT_READY);
    uint8_t empty[32] = {0};
    assert(!tcs_admin_init(&zero, empty, realm, boot));
    assert(!tcs_admin_init(&zero, key, empty, boot));
    assert(!tcs_admin_init(&zero, key, realm, empty));
    puts("PASS authenticated malformed commands and unconfigured state rejected");
}
static struct tcs_result execute(struct tcs_admin *a, struct tcs_policy *live,
    struct tcs_admin_command c, bool audit)
{
    uint8_t p[192]; sign_packet(p, c); struct tcs_admin_command admitted;
    assert(tcs_admin_admit(a, p, 192, &admitted) == TCS_ADMIN_ACCEPTED);
    struct tcs_policy candidate = *live;
    struct tcs_result decision = tcs_policy_admin_compare_apply(&candidate, TCS_ADMIN, admitted);
    struct tcs_result result = tcs_policy_commit(live, &candidate, admitted.request, decision, audit);
    /* This local model has a definite receipt even when audit failed. */
    assert(tcs_admin_complete(a, admitted.sequence) == TCS_ADMIN_ACCEPTED);
    return result;
}
static void policy_integration(void)
{
    struct tcs_admin a = fresh(); struct tcs_policy live = {0};
    assert(execute(&a, &live, grant, true).status == TCS_OK);
    struct tcs_policy before = live;
    struct tcs_admin_command c = grant; c.sequence = 2; /* stale generation 0 */
    assert(execute(&a, &live, c, true).status == TCS_STALE);
    assert(memcmp(&live, &before, sizeof live) == 0 && a.next_sequence == 3);
    c = (struct tcs_admin_command){3, 1, {TCS_REVOKE, 1, 0, 0, 0}};
    assert(execute(&a, &live, c, false).status == TCS_AUDIT_FULL);
    assert(live.subjects[1].state == TCS_RESTRICTED && live.subjects[1].generation == 2);
    c = grant; c.sequence = 4; c.expected_generation = 2; before = live;
    assert(execute(&a, &live, c, false).status == TCS_AUDIT_FULL);
    assert(memcmp(&live, &before, sizeof live) == 0);
    c.sequence = 5; assert(execute(&a, &live, c, true).status == TCS_OK);
    c = (struct tcs_admin_command){6, 3, {TCS_QUARANTINE, 1, 0, 0, 0}};
    assert(execute(&a, &live, c, true).status == TCS_OK);
    c = (struct tcs_admin_command){7, 4, {TCS_RESTORE, 1, 0, 0, 0}};
    assert(execute(&a, &live, c, true).status == TCS_OK);
    assert(live.subjects[1].state == TCS_RESTRICTED && live.subjects[1].generation == 5);
    before = live;
    assert(tcs_policy_admin_compare_apply(&live, TCS_STORAGE, c).status == TCS_DENIED);
    c.request.subject = TCS_SUBJECTS;
    assert(tcs_policy_admin_compare_apply(&live, TCS_ADMIN, c).status == TCS_BAD_MESSAGE);
    assert(memcmp(&live, &before, sizeof live) == 0);
    a = fresh(); live = (struct tcs_policy){0};
    uint8_t p[192]; sign_packet(p, grant); struct tcs_admin_command admitted;
    assert(tcs_admin_admit(&a, p, 192, &admitted) == TCS_ADMIN_ACCEPTED);
    assert(tcs_policy_apply(&live, TCS_ADMIN,
        (struct tcs_request){TCS_QUARANTINE, 1, 0, 0, 0}).status == TCS_OK);
    before = live; /* State changed AFTER authentication but BEFORE policy execution. */
    assert(tcs_policy_admin_compare_apply(&live, TCS_ADMIN, admitted).status == TCS_STALE);
    assert(memcmp(&live, &before, sizeof live) == 0);
    puts("PASS atomic policy generation precondition, definite receipts, audit failure, revoke/quarantine/restore");
}
int main(void)
{
    rfc8032(); encoding_and_tampering(); replay_and_context();
    malformed_signed_commands(); policy_integration();
    crypto_wipe(secret, sizeof secret);
    puts("TCS SIGNED ADMIN CORE TESTS PASS (host model; no live administration endpoint)");
}
