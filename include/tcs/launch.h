#ifndef TCS_LAUNCH_H
#define TCS_LAUNCH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCS_PUBLIC_BYTES 80u
#define TCS_LAUNCH_BYTES 112u
enum tcs_launch_mode { TCS_OPERATOR_MODE, TCS_FIXTURE_MODE };
struct tcs_identity { uint8_t realm[32], public_key[32]; };
struct tcs_launch { struct tcs_identity identity; uint8_t boot[32]; };

/* Format checks, NOT authorization, entropy, freshness, or arbitrary-key
 * validation. The expected mode is trusted configuration, never wire input.
 * Fixture mode accepts only RFC 8032 test keys 1-3; operator mode refuses them.
 * A blocklist cannot establish that any other key is privately held. */
bool tcs_public_encode(uint8_t out[TCS_PUBLIC_BYTES], struct tcs_identity identity,
    enum tcs_launch_mode mode);
bool tcs_public_decode(const uint8_t *bytes, size_t length, enum tcs_launch_mode mode,
    struct tcs_identity *out);
bool tcs_launch_encode(uint8_t out[TCS_LAUNCH_BYTES], struct tcs_launch launch,
    enum tcs_launch_mode mode);
bool tcs_launch_decode(const uint8_t *bytes, size_t length, enum tcs_launch_mode mode,
    struct tcs_launch *out);
/* Inputs/outputs do not alias. Invalid outputs are zeroed. */
#endif
