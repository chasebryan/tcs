/* Public RFC fixture only. Linked exclusively into the test executable. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
ssize_t tcs_operator_test_write(int fd, const void *data, size_t size)
{
    static unsigned calls;
    if (getenv("TCS_TEST_WRITE_FAIL") && calls++) { errno = EIO; return -1; }
    if (getenv("TCS_TEST_WRITE_FAIL") || getenv("TCS_TEST_SHORT_WRITE")) if (size > 7) size = 7;
    return write(fd, data, size);
}
int tcs_operator_test_fsync(int fd)
{
    int result = fsync(fd);
    if (getenv("TCS_TEST_FSYNC_FAIL")) { errno = EIO; return -1; }
    return result;
}
int tcs_operator_test_entropy(void *output, size_t size)
{
    static const uint8_t seed[32] = {0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,
        0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60};
    if (getenv("TCS_TEST_ENTROPY_ERROR")) return -1;
    uint8_t *bytes = output;
    if (size == 64) { memcpy(bytes, seed, 32); memset(bytes + 32, 0x54, 32); }
    else if (size == 32) {
        const char *nonce = getenv("TCS_TEST_NONCE_HEX");
        if (nonce) {
            if (strlen(nonce) != 64) return -1;
            for (unsigned i = 0; i < 64; ++i) {
                char c = nonce[i]; unsigned n;
                if (c >= '0' && c <= '9') n = (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') n = (unsigned)(c - 'a' + 10);
                else return -1;
                if (i % 2) bytes[i/2] |= (uint8_t)n; else bytes[i/2] = (uint8_t)(n << 4);
            }
        } else memset(bytes, getenv("TCS_TEST_SECOND_BOOT") ? 0x43 : 0x42, 32);
    }
    else return -1;
    return 0;
}
