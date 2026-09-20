#ifndef TCS_ISOLATION_CASES_H
#define TCS_ISOLATION_CASES_H
#include <stdbool.h>
#include <stdint.h>

/* Test-only addresses and canary; never a secret or an authorization token. */
#define ISO_CANARY UINT64_C(0x7463735f69736f31)
#define ISO_CASES 6u
#define ISO_VM_FAULT 6u
#define ISO_FAULT_WORDS 4u
struct iso_fault { uint64_t label, count, ip, address, instruction, fsr; };
struct iso_case { const char *name; uint64_t address; bool write, execute, permission; };
static const struct iso_case iso_cases[ISO_CASES] = {
    {"uart-physical-read", 0x09000018, false, false, false},
    {"uart-alias-write",   0x04000000, true,  false, false},
    {"policy-page-read",   0x05000000, false, false, false},
    {"policy-page-write",  0x05000000, true,  false, false},
    {"readonly-write",     0x06000000, true,  false, true},
    {"nonexec-fetch",      0x07000000, false, true,  true},
};

static inline bool iso_fault_matches(unsigned child, struct iso_fault f)
{
    if (child < 1 || child > ISO_CASES || f.label != ISO_VM_FAULT ||
        f.count != ISO_FAULT_WORDS || f.fsr >> 32 || (f.ip & 3))
        return false;
    const struct iso_case *c = &iso_cases[child - 1];
    uint64_t ec = (f.fsr >> 26) & 0x3f, fsc = f.fsr & 0x3f;
    /* AArch64 ESR: lower-EL abort, 32-bit instruction, valid address; no
     * external abort, cache maintenance, or stage-1 page-table-walk fault.
     * See pinned Microkit monitor's AArch64 syndrome decoder. */
    if (f.address != c->address || f.instruction != (uint64_t)c->execute ||
        ec != (c->execute ? 0x20u : 0x24u) || !(f.fsr & (1u << 25)) ||
        (f.fsr & (15u << 7)) || ((f.fsr >> 6) & 1) != (uint64_t)c->write)
        return false;
    if (c->permission ? (fsc < 13 || fsc > 15) : (fsc < 4 || fsc > 7))
        return false;
    return c->execute ? f.ip == c->address : (f.ip >= 0x200000 && f.ip < 0x300000);
}
#endif
