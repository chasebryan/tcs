#include "tcs/boot.h"

static bool read_bytes(struct tcs_fwcfg_io io, uint8_t *out, size_t length)
{
    for (size_t i = 0; i < length; ++i) if (!io.read(io.context, out + i)) return false;
    return true;
}
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static enum tcs_boot_status read_item(struct tcs_fwcfg_io io, const char *name,
    uint8_t *out, size_t length)
{
    if (!io.select || !io.read || !name || !out || !length || length > TCS_FWCFG_MAX_BYTES)
        return TCS_BOOT_FORMAT;
    size_t name_length = 0;
    while (name_length < 56 && name[name_length]) ++name_length;
    if (!name_length || name_length == 56) return TCS_BOOT_FORMAT;
    uint8_t field[64];
    if (!io.select(io.context, 0) || !read_bytes(io, field, 4)) return TCS_BOOT_IO;
    if (field[0] != 'Q' || field[1] != 'E' || field[2] != 'M' || field[3] != 'U')
        return TCS_BOOT_DEVICE;
    if (!io.select(io.context, 1) || !read_bytes(io, field, 4)) return TCS_BOOT_IO;
    /* Exact traditional interface only: refuse DMA and unknown features. */
    if (field[0] != 1 || field[1] || field[2] || field[3]) return TCS_BOOT_FEATURES;
    if (!io.select(io.context, 0x19) || !read_bytes(io, field, 4)) return TCS_BOOT_IO;
    uint32_t count = be32(field);
    if (count > TCS_FWCFG_MAX_FILES) return TCS_BOOT_DIRECTORY;
    uint16_t selectors[TCS_FWCFG_MAX_FILES], selected = 0;
    uint32_t selected_size = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!read_bytes(io, field, 64)) return TCS_BOOT_IO;
        uint16_t key = (uint16_t)((uint16_t)field[4] << 8 | field[5]);
        if (key < 0x20 || key >= 0x4000 || field[6] || field[7]) return TCS_BOOT_DIRECTORY;
        for (uint32_t j = 0; j < i; ++j) if (selectors[j] == key) return TCS_BOOT_DIRECTORY;
        selectors[i] = key;
        size_t n = 0;
        while (n < 56 && field[8 + n]) ++n;
        if (!n || n == 56) return TCS_BOOT_DIRECTORY;
        bool match = n == name_length;
        for (size_t j = 0; match && j < n; ++j) match = field[8 + j] == (uint8_t)name[j];
        if (match) {
            if (selected) return TCS_BOOT_DIRECTORY;
            selected = key; selected_size = be32(field);
        }
    }
    if (!selected) return TCS_BOOT_MISSING;
    if (selected_size != length) return TCS_BOOT_LENGTH;
    if (!io.select(io.context, selected) || !read_bytes(io, out, length)) return TCS_BOOT_IO;
    return TCS_BOOT_OK;
}
enum tcs_boot_status tcs_fwcfg_read(struct tcs_fwcfg_io io, const char *name,
    uint8_t *output, size_t length)
{
    enum tcs_boot_status status = read_item(io, name, output, length);
    if (status != TCS_BOOT_OK && output && length <= TCS_FWCFG_MAX_BYTES)
        for (size_t i = 0; i < length; ++i) output[i] = 0;
    return status;
}
enum tcs_boot_status tcs_boot_decode_test(const uint8_t *bytes, size_t length,
    struct tcs_boot_context *out)
{
    static const uint8_t prefix[16] = {'T','C','S','-','B','O','O','T',1,1,0,0,0,0,0,0};
    if (!out) return TCS_BOOT_FORMAT;
    *out = (struct tcs_boot_context){0};
    if (!bytes || length != TCS_BOOT_BYTES) return TCS_BOOT_LENGTH;
    for (size_t i = 0; i < 16; ++i) if (bytes[i] != prefix[i]) return TCS_BOOT_FORMAT;
    for (size_t start = 16; start < TCS_BOOT_BYTES; start += 32) {
        uint8_t any = 0;
        for (size_t i = 0; i < 32; ++i) any |= bytes[start + i];
        if (!any) return TCS_BOOT_FORMAT;
    }
    for (size_t i = 0; i < 32; ++i) {
        out->realm[i] = bytes[16+i]; out->boot[i] = bytes[48+i]; out->public_key[i] = bytes[80+i];
    }
    return TCS_BOOT_OK;
}
