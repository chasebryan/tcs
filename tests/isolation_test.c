#include <assert.h>
#include <stdio.h>
#include "isolation/cases.h"

int main(void)
{
    for (unsigned child = 1; child <= ISO_CASES; ++child) {
        const struct iso_case *c = &iso_cases[child - 1];
        struct iso_fault f = {ISO_VM_FAULT, ISO_FAULT_WORDS,
            c->execute ? c->address : 0x200004, c->address, c->execute,
            ((c->execute ? UINT64_C(0x20) : UINT64_C(0x24)) << 26) |
            (1u << 25) | ((uint64_t)c->write << 6) | (c->permission ? 15u : 6u)};
        assert(iso_fault_matches(child, f));
        struct iso_fault bad = f; bad.label = 0; assert(!iso_fault_matches(child, bad));
        for (unsigned words = 0; words <= 64; ++words) {
            bad = f; bad.count = words;
            assert(iso_fault_matches(child, bad) == (words == ISO_FAULT_WORDS));
        }
        bad = f; ++bad.address; assert(!iso_fault_matches(child, bad));
        bad = f; bad.instruction ^= 1; assert(!iso_fault_matches(child, bad));
        bad = f; ++bad.ip; assert(!iso_fault_matches(child, bad));
        bad = f; bad.ip = 0; assert(!iso_fault_matches(child, bad));
        for (unsigned bit = 6; bit <= 10; ++bit) {
            bad = f; bad.fsr ^= UINT64_C(1) << bit;
            assert(!iso_fault_matches(child, bad));
        }
        for (unsigned ec = 0; ec < 64; ++ec) {
            bad = f; bad.fsr = (f.fsr & ~(UINT64_C(63) << 26)) | ((uint64_t)ec << 26);
            assert(iso_fault_matches(child, bad) == (ec == (c->execute ? 0x20u : 0x24u)));
        }
        for (unsigned fsc = 0; fsc < 64; ++fsc) {
            bad = f; bad.fsr = (f.fsr & ~UINT64_C(63)) | fsc;
            bool valid = c->permission ? fsc >= 13 && fsc <= 15 : fsc >= 4 && fsc <= 7;
            assert(iso_fault_matches(child, bad) == valid);
        }
        bad = f; bad.fsr &= ~(UINT64_C(1) << 25); assert(!iso_fault_matches(child, bad));
        bad = f; bad.fsr |= UINT64_C(1) << 32; assert(!iso_fault_matches(child, bad));
        assert(!iso_fault_matches(0, f) && !iso_fault_matches(7, f));
    }
    puts("TCS ISOLATION FAULT DECODER TESTS PASS (fault type, address, access, syndrome, shape)");
}
