#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/launch.h"

static unsigned nibble(char c) { return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10); }
static void key(uint8_t out[32], const char *hex)
{ for (unsigned i = 0; i < 32; ++i) out[i] = (uint8_t)(16*nibble(hex[2*i]) + nibble(hex[2*i+1])); }
static bool zero(const void *data, size_t length)
{ const uint8_t *p = data; for (size_t i = 0; i < length; ++i) if (p[i]) return false; return true; }
int main(void)
{
    const char *fixtures[] = {
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025"};
    struct tcs_launch launch = {{{0x54},{0}}, {0x42}}, out;
    struct tcs_identity identity;
    uint8_t public[81], context[113], copy[113];
    for (unsigned k = 0; k < 3; ++k) {
        key(launch.identity.public_key, fixtures[k]);
        assert(!tcs_public_encode(public, launch.identity, TCS_OPERATOR_MODE) && zero(public, 80));
        assert(!tcs_launch_encode(context, launch, TCS_OPERATOR_MODE) && zero(context, 112));
        assert(tcs_public_encode(public, launch.identity, TCS_FIXTURE_MODE));
        assert(tcs_launch_encode(context, launch, TCS_FIXTURE_MODE));
        assert(tcs_public_decode(public, 80, TCS_FIXTURE_MODE, &identity));
        assert(tcs_launch_decode(context, 112, TCS_FIXTURE_MODE, &out));
        assert(!memcmp(&identity, &launch.identity, sizeof identity) && !memcmp(&out, &launch, sizeof out));
        public[15] = context[15] = 0; /* Changing mode does not promote RFC fixtures. */
        assert(!tcs_public_decode(public, 80, TCS_OPERATOR_MODE, &identity) && zero(&identity, sizeof identity));
        assert(!tcs_launch_decode(context, 112, TCS_OPERATOR_MODE, &out) && zero(&out, sizeof out));
    }
    key(launch.identity.public_key, fixtures[0]);
    assert(tcs_launch_encode(context, launch, TCS_FIXTURE_MODE));
    assert(tcs_public_encode(public, launch.identity, TCS_FIXTURE_MODE));
    for (size_t n = 0; n <= 113; ++n) {
        if (n != 112) assert(!tcs_launch_decode(context, n, TCS_FIXTURE_MODE, &out) && zero(&out, sizeof out));
        if (n != 80) assert(!tcs_public_decode(public, n, TCS_FIXTURE_MODE, &identity) && zero(&identity, sizeof identity));
    }
    for (unsigned i = 0; i < 16; ++i) for (unsigned bit = 0; bit < 8; ++bit) {
        memcpy(copy, context, 112); copy[i] ^= (uint8_t)(1u << bit);
        assert(!tcs_launch_decode(copy, 112, TCS_FIXTURE_MODE, &out) && zero(&out, sizeof out));
        memcpy(copy, public, 80); copy[i] ^= (uint8_t)(1u << bit);
        assert(!tcs_public_decode(copy, 80, TCS_FIXTURE_MODE, &identity) && zero(&identity, sizeof identity));
    }
    for (unsigned offset = 16; offset <= 80; offset += 32) {
        memcpy(copy, context, 112); memset(copy + offset, 0, 32);
        assert(!tcs_launch_decode(copy, 112, TCS_FIXTURE_MODE, &out) && zero(&out, sizeof out));
    }
    assert(!tcs_launch_decode(NULL, 112, TCS_FIXTURE_MODE, &out));
    assert(!tcs_public_decode(NULL, 80, TCS_FIXTURE_MODE, &identity));
    assert(!tcs_launch_decode(context, 112, (enum tcs_launch_mode)2, &out));
    assert(!tcs_public_decode(public, 80, (enum tcs_launch_mode)2, &identity));
    /* Non-fixture structural bytes deliberately need not encode a curve point.
     * The host signer derives/compares its actual key; parsing is NOT key validation. */
    memset(launch.identity.public_key, 1, 32);
    assert(tcs_launch_encode(context, launch, TCS_OPERATOR_MODE));
    assert(tcs_launch_decode(context, 112, TCS_OPERATOR_MODE, &out));
    assert(!tcs_launch_decode(context, 112, TCS_FIXTURE_MODE, &out));
    assert(tcs_public_encode(public, launch.identity, TCS_OPERATOR_MODE));
    assert(tcs_public_decode(public, 80, TCS_OPERATOR_MODE, &identity));
    assert(!tcs_public_decode(public, 80, TCS_FIXTURE_MODE, &identity));
    assert(!tcs_public_encode(public, launch.identity, TCS_FIXTURE_MODE) && zero(public, 80));
    assert(!tcs_launch_encode(context, launch, TCS_FIXTURE_MODE) && zero(context, 112));
    puts("PASS launch/public formats: lengths, 256 header corruptions, modes, zero error outputs, three RFC key exclusions");
}
