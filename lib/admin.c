#include "tcs/admin.h"
#include "monocypher-ed25519.h"

static const uint8_t domain[16] = {'T','C','S','-','A','D','M','I','N','-',1,0,0,0,0,0};

static bool equal(const uint8_t *a, const uint8_t *b, size_t length)
{
    /* These fields and comparison results are public; no timing secrecy claim. */
    for (size_t i = 0; i < length; ++i) if (a[i] != b[i]) return false;
    return true;
}
static bool nonzero(const uint8_t bytes[32])
{
    if (!bytes) return false;
    uint8_t any = 0;
    for (size_t i = 0; i < 32; ++i) any |= bytes[i];
    return any != 0;
}
static void copy(uint8_t *to, const uint8_t *from, size_t length)
{
    for (size_t i = 0; i < length; ++i) to[i] = from[i];
}
static uint64_t load64(const uint8_t *p)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8 * i);
    return value;
}
static void store64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(value >> (8 * i));
}
static bool valid(struct tcs_admin_command c)
{
    struct tcs_request r = c.request;
    if (!c.sequence || r.subject >= TCS_SUBJECTS || r.generation != 0) return false;
    if (r.op == TCS_GRANT) return r.object == TCS_OBJECT && r.rights == TCS_READ;
    return (r.op == TCS_REVOKE || r.op == TCS_QUARANTINE || r.op == TCS_RESTORE) &&
           r.object == 0 && r.rights == 0;
}

bool tcs_admin_init(struct tcs_admin *a, const uint8_t key[32],
    const uint8_t realm[32], const uint8_t boot[32])
{
    if (!a || a->initialized || !nonzero(key) || !nonzero(realm) || !nonzero(boot)) return false;
    *a = (struct tcs_admin){0};
    copy(a->public_key, key, 32); copy(a->realm, realm, 32); copy(a->boot, boot, 32);
    a->next_sequence = 1; a->initialized = true;
    return true;
}

bool tcs_admin_encode(uint8_t message[128], const uint8_t realm[32],
    const uint8_t boot[32], struct tcs_admin_command c)
{
    if (!message) return false;
    for (size_t i = 0; i < 128; ++i) message[i] = 0;
    if (!nonzero(realm) || !nonzero(boot) || !valid(c)) return false;
    copy(message, domain, 16); copy(message + 16, realm, 32); copy(message + 48, boot, 32);
    store64(message + 80, c.sequence); store64(message + 88, c.request.op);
    store64(message + 96, c.request.subject); store64(message + 104, c.request.object);
    store64(message + 112, c.request.rights); store64(message + 120, c.expected_generation);
    return true;
}

enum tcs_admin_status tcs_admin_admit(struct tcs_admin *a, const uint8_t *packet,
    size_t length, struct tcs_admin_command *command)
{
    if (!command) return TCS_ADMIN_BAD_PACKET;
    *command = (struct tcs_admin_command){0};
    if (!a || !a->initialized) return TCS_ADMIN_NOT_READY;
    if (a->pending_sequence) return TCS_ADMIN_BUSY;
    if (a->exhausted) return TCS_ADMIN_EXHAUSTED;
    if (!packet || length != TCS_ADMIN_PACKET_BYTES || !equal(packet, domain, 16))
        return TCS_ADMIN_BAD_PACKET;
    struct tcs_admin_command c = {load64(packet + 80), load64(packet + 120),
        {load64(packet + 88), load64(packet + 96), load64(packet + 104), load64(packet + 112), 0}};
    if (!valid(c)) return TCS_ADMIN_BAD_PACKET;
    if (!equal(packet + 16, a->realm, 32) || !equal(packet + 48, a->boot, 32))
        return TCS_ADMIN_BAD_CONTEXT;
    if (c.sequence != a->next_sequence) return TCS_ADMIN_REPLAY;
    if (crypto_ed25519_check(packet + 128, a->public_key, packet, 128) != 0)
        return TCS_ADMIN_BAD_SIGNATURE;
    a->pending_sequence = c.sequence;
    if (c.sequence == UINT64_MAX) a->exhausted = true;
    else ++a->next_sequence;
    *command = c;
    return TCS_ADMIN_ACCEPTED;
}

enum tcs_admin_status tcs_admin_complete(struct tcs_admin *a, uint64_t sequence)
{
    if (!a || !a->initialized) return TCS_ADMIN_NOT_READY;
    if (!sequence || a->pending_sequence != sequence) return TCS_ADMIN_BAD_COMPLETION;
    a->pending_sequence = 0;
    return TCS_ADMIN_ACCEPTED;
}
