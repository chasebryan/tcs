#include "tcs/launch.h"

static const uint8_t public_prefix[16] = {'T','C','S','-','P','U','B',1};
static const uint8_t launch_prefix[16] = {'T','C','S','-','L','A','U','N','C','H',1};
static bool nonzero(const uint8_t bytes[32])
{
    uint8_t any = 0;
    for (unsigned i = 0; i < 32; ++i) any |= bytes[i];
    return any != 0;
}
static void copy(uint8_t *to, const uint8_t *from, size_t size)
{ for (size_t i = 0; i < size; ++i) to[i] = from[i]; }
static bool header(const uint8_t *bytes, const uint8_t prefix[16], enum tcs_launch_mode mode)
{
    if (mode != TCS_OPERATOR_MODE && mode != TCS_FIXTURE_MODE) return false;
    for (unsigned i = 0; i < 15; ++i) if (bytes[i] != prefix[i]) return false;
    return bytes[15] == (uint8_t)mode;
}
static bool identity_valid(struct tcs_identity identity, enum tcs_launch_mode mode)
{
    static const char *const fixtures[] = {
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025"};
    static const char hex[] = "0123456789abcdef";
    if (!nonzero(identity.realm) || !nonzero(identity.public_key)) return false;
    bool fixture = false;
    for (unsigned k = 0; k < 3; ++k) {
        bool match = true;
        for (unsigned i = 0; i < 32; ++i)
            match = match && fixtures[k][2*i] == hex[identity.public_key[i] >> 4] &&
                fixtures[k][2*i+1] == hex[identity.public_key[i] & 15];
        fixture = fixture || match;
    }
    return mode == TCS_FIXTURE_MODE ? fixture : mode == TCS_OPERATOR_MODE && !fixture;
}
bool tcs_public_encode(uint8_t out[80], struct tcs_identity identity, enum tcs_launch_mode mode)
{
    if (!out) return false;
    for (unsigned i = 0; i < 80; ++i) out[i] = 0;
    if (!identity_valid(identity, mode)) return false;
    copy(out, public_prefix, 16); out[15] = (uint8_t)mode;
    copy(out + 16, identity.realm, 32); copy(out + 48, identity.public_key, 32);
    return true;
}
bool tcs_public_decode(const uint8_t *bytes, size_t length, enum tcs_launch_mode mode,
    struct tcs_identity *out)
{
    if (!out) return false;
    *out = (struct tcs_identity){0};
    if (!bytes || length != 80 || !header(bytes, public_prefix, mode)) return false;
    struct tcs_identity identity;
    copy(identity.realm, bytes + 16, 32); copy(identity.public_key, bytes + 48, 32);
    if (!identity_valid(identity, mode)) return false;
    *out = identity; return true;
}
bool tcs_launch_encode(uint8_t out[112], struct tcs_launch launch, enum tcs_launch_mode mode)
{
    if (!out) return false;
    for (unsigned i = 0; i < 112; ++i) out[i] = 0;
    if (!identity_valid(launch.identity, mode) || !nonzero(launch.boot)) return false;
    copy(out, launch_prefix, 16); out[15] = (uint8_t)mode;
    copy(out + 16, launch.identity.realm, 32); copy(out + 48, launch.boot, 32);
    copy(out + 80, launch.identity.public_key, 32); return true;
}
bool tcs_launch_decode(const uint8_t *bytes, size_t length, enum tcs_launch_mode mode,
    struct tcs_launch *out)
{
    if (!out) return false;
    *out = (struct tcs_launch){0};
    if (!bytes || length != 112 || !header(bytes, launch_prefix, mode)) return false;
    struct tcs_launch launch;
    copy(launch.identity.realm, bytes + 16, 32); copy(launch.boot, bytes + 48, 32);
    copy(launch.identity.public_key, bytes + 80, 32);
    if (!identity_valid(launch.identity, mode) || !nonzero(launch.boot)) return false;
    *out = launch; return true;
}
