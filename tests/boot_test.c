#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tcs/boot.h"

struct mock {
    uint8_t signature[4], features[4], directory[4 + 64 * 64], payload[256];
    uint16_t selected;
    size_t offset, calls, fail_at, directory_size, payload_size;
};
static bool select_item(void *context, uint16_t key)
{
    struct mock *m = context;
    if (++m->calls == m->fail_at) return false;
    m->selected = key; m->offset = 0; return true;
}
static bool read_byte(void *context, uint8_t *out)
{
    struct mock *m = context;
    if (++m->calls == m->fail_at) return false;
    const uint8_t *p; size_t n;
    switch (m->selected) {
    case 0: p = m->signature; n = 4; break;
    case 1: p = m->features; n = 4; break;
    case 0x19: p = m->directory; n = m->directory_size; break;
    default: p = m->payload; n = m->payload_size; break;
    }
    /* Like real fw_cfg, reading beyond an item yields zeros, not an error. */
    *out = m->offset < n ? p[m->offset] : 0; ++m->offset; return true;
}
static struct mock fresh(void)
{
    struct mock m = {.signature={'Q','E','M','U'}, .features={1}, .directory_size=68, .payload_size=112};
    m.directory[3] = 1; m.directory[7] = 112; m.directory[9] = 0x20;
    memcpy(m.directory + 12, TCS_BOOT_ITEM, sizeof TCS_BOOT_ITEM);
    const uint8_t prefix[16] = {'T','C','S','-','B','O','O','T',1,1};
    memcpy(m.payload, prefix, 16); m.payload[16] = 1; m.payload[48] = 2; m.payload[80] = 3;
    return m;
}
static enum tcs_boot_status run(struct mock *m, uint8_t out[112])
{
    return tcs_fwcfg_read((struct tcs_fwcfg_io){m, select_item, read_byte}, TCS_BOOT_ITEM, out, 112);
}
static void denied(struct mock m, enum tcs_boot_status expected)
{
    uint8_t out[112]; memset(out, 0xa5, sizeof out);
    assert(run(&m, out) == expected);
    for (size_t i = 0; i < sizeof out; ++i) assert(out[i] == 0);
}
int main(void)
{
    struct mock m = fresh(); uint8_t out[112];
    assert(run(&m, out) == TCS_BOOT_OK && memcmp(out, m.payload, 112) == 0);
    size_t calls = m.calls;
    for (unsigned count = 2; count <= 64; ++count) {
        m = fresh(); m.directory[3] = (uint8_t)count; m.directory_size = 4 + 64 * count;
        for (unsigned i = 1; i < count; ++i) {
            uint8_t *entry = m.directory + 4 + 64 * i;
            memcpy(entry, m.directory + 4, 64); entry[5] = (uint8_t)(0x20 + i);
            entry[8] = 'x'; /* Unrelated names may repeat; selectors must not alias. */
        }
        assert(run(&m, out) == TCS_BOOT_OK && memcmp(out, m.payload, 112) == 0);
    }
    for (size_t i = 1; i <= calls; ++i) { m = fresh(); m.fail_at = i; denied(m, TCS_BOOT_IO); }
    m = fresh(); m.signature[0] = 0; denied(m, TCS_BOOT_DEVICE);
    for (unsigned f = 0; f < 256; ++f) if (f != 1) {
        m = fresh(); m.features[0] = (uint8_t)f; denied(m, TCS_BOOT_FEATURES);
    }
    for (unsigned i = 1; i < 4; ++i) { m = fresh(); m.features[i] = 1; denied(m, TCS_BOOT_FEATURES); }
    m = fresh(); m.directory[0] = 1; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[3] = 65; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[3] = 0; denied(m, TCS_BOOT_MISSING);
    m = fresh(); m.directory[12] = 'x'; denied(m, TCS_BOOT_MISSING);
    m = fresh(); m.directory[10] = 1; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[11] = 1; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[8] = 0x40; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[9] = 0x19; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); memset(m.directory + 12, 'a', 56); denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[12] = 0; denied(m, TCS_BOOT_DIRECTORY);
    m = fresh(); m.directory[7] = 111; denied(m, TCS_BOOT_LENGTH);
    m = fresh(); m.directory[7] = 113; denied(m, TCS_BOOT_LENGTH);
    m = fresh(); m.directory[4] = 1; denied(m, TCS_BOOT_LENGTH);
    m = fresh(); m.directory[3] = 2; m.directory_size = 132;
    memcpy(m.directory + 68, m.directory + 4, 64); denied(m, TCS_BOOT_DIRECTORY); /* selector alias */
    m.directory[73] = 0x21; denied(m, TCS_BOOT_DIRECTORY); /* duplicate name */
    m = fresh(); m.directory_size = 10; denied(m, TCS_BOOT_DIRECTORY);
    struct tcs_boot_context decoded;
    m = fresh(); assert(tcs_boot_decode_test(m.payload, 112, &decoded) == TCS_BOOT_OK);
    assert(decoded.realm[0] == 1 && decoded.boot[0] == 2 && decoded.public_key[0] == 3);
    for (size_t n = 0; n <= 113; ++n) if (n != 112)
        assert(tcs_boot_decode_test(m.payload, n, &decoded) == TCS_BOOT_LENGTH);
    for (size_t i = 0; i < 16; ++i) for (unsigned bit = 0; bit < 8; ++bit) {
        m = fresh(); m.payload[i] ^= (uint8_t)(1u << bit);
        assert(tcs_boot_decode_test(m.payload, 112, &decoded) == TCS_BOOT_FORMAT);
        struct tcs_boot_context zero = {0}; assert(memcmp(&decoded, &zero, sizeof zero) == 0);
    }
    for (size_t i = 16; i < 112; i += 32) {
        m = fresh(); memset(m.payload + i, 0, 32);
        assert(tcs_boot_decode_test(m.payload, 112, &decoded) == TCS_BOOT_FORMAT);
    }
    assert(tcs_boot_decode_test(NULL, 112, &decoded) == TCS_BOOT_LENGTH);
    assert(tcs_boot_decode_test(m.payload, 112, NULL) == TCS_BOOT_FORMAT);
    struct tcs_fwcfg_io io = {&m, select_item, read_byte};
    assert(tcs_fwcfg_read(io, TCS_BOOT_ITEM, out, 257) == TCS_BOOT_FORMAT);
    assert(tcs_fwcfg_read(io, TCS_BOOT_ITEM, NULL, 112) == TCS_BOOT_FORMAT);
    assert(tcs_fwcfg_read(io, NULL, out, 112) == TCS_BOOT_FORMAT);
    io.read = NULL; assert(tcs_fwcfg_read(io, TCS_BOOT_ITEM, out, 112) == TCS_BOOT_FORMAT);
    puts("TCS BOOT CORE TESTS PASS (bounded directory, exact lengths, DMA rejection, I/O failures, test-only context)");
}
