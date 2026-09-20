/* Public RFC 8032 test vector 1 only. NOT an operator signing/provisioning tool. */
#include <stdio.h>
#include "tcs/admin.h"
#include "tcs/boot.h"
#include "monocypher-ed25519.h"
int main(void)
{
    uint8_t seed[32] = {0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60};
    uint8_t secret[64], context[112] = {'T','C','S','-','B','O','O','T',1,1}, packet[192];
    context[16] = 0x54; /* Test realm, never a deployed identity. */
    if (fread(context + 48, 1, 32, stdin) != 32 || getchar() != EOF) return 1;
    crypto_ed25519_key_pair(secret, context + 80, seed);
    struct tcs_boot_context decoded;
    if (tcs_boot_decode_test(context, sizeof context, &decoded) != TCS_BOOT_OK) return 1;
    struct tcs_admin_command command = {1, 0, {TCS_GRANT, 1, TCS_OBJECT, TCS_READ, 0}};
    if (!tcs_admin_encode(packet, decoded.realm, decoded.boot, command)) return 1;
    crypto_ed25519_sign(packet + 128, secret, packet, 128);
    crypto_wipe(secret, sizeof secret);
    if (fwrite(context, 1, sizeof context, stdout) != sizeof context ||
        fwrite(packet, 1, sizeof packet, stdout) != sizeof packet || fflush(stdout)) return 1;
    return 0;
}
