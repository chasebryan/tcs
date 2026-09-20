#ifndef TCS_BOOT_H
#define TCS_BOOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCS_BOOT_BYTES 112u
#define TCS_BOOT_ITEM "opt/tcs/test-context"
#define TCS_BOOT_COMMAND_ITEM "opt/tcs/test-command"
#define TCS_FWCFG_MAX_FILES 64u
#define TCS_FWCFG_MAX_BYTES 256u

enum tcs_boot_status { TCS_BOOT_OK, TCS_BOOT_IO, TCS_BOOT_DEVICE,
    TCS_BOOT_FEATURES, TCS_BOOT_DIRECTORY, TCS_BOOT_MISSING,
    TCS_BOOT_LENGTH, TCS_BOOT_FORMAT };

/* Exclusive, synchronous device owner. No concurrently shared selector state.
 * select receives a host-endian selector; the MMIO adapter converts to BE16.
 * Output buffers must not alias context, callbacks, or device storage. */
struct tcs_fwcfg_io {
    void *context;
    bool (*select)(void *context, uint16_t key);
    bool (*read)(void *context, uint8_t *byte);
};
enum tcs_boot_status tcs_fwcfg_read(struct tcs_fwcfg_io io, const char *name,
    uint8_t *output, size_t length);

/* This first boot format is explicitly TEST-ONLY, not operator provisioning.
 * Nonzero fields do not establish entropy, key validity, or authorization. */
struct tcs_boot_context { uint8_t realm[32], boot[32], public_key[32]; };
enum tcs_boot_status tcs_boot_decode_test(const uint8_t *bytes, size_t length,
    struct tcs_boot_context *output);
#endif
