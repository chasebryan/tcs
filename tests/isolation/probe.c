#include "tcs/ipc.h"
#include "cases.h"
#if !defined(TCS_RELEASE_PROFILE) || !defined(ISO_PROBE) || ISO_PROBE < 1 || ISO_PROBE > 6
#error "Isolation probe requires a numbered release-kernel test build"
#endif

void init(void)
{
    uintptr_t address = iso_cases[ISO_PROBE - 1].address;
    /* Inline assembly makes the forbidden operation explicit, without relying
     * on C's behavior for dereferencing a deliberately unowned address. If it
     * unexpectedly succeeds, BRK generates a different fault and must fail. */
#if ISO_PROBE == 6
    *(volatile uint32_t *)address = UINT32_C(0xd65f03c0); /* RET, but page is NX. */
    __asm__ volatile("blr %0\n\tbrk #0" :: "r"(address) : "x30", "memory");
#elif ISO_PROBE == 2 || ISO_PROBE == 4 || ISO_PROBE == 5
    __asm__ volatile("str xzr, [%0]\n\tbrk #0" :: "r"(address) : "memory");
#else
    __asm__ volatile("ldr x0, [%0]\n\tbrk #0" :: "r"(address) : "x0", "memory");
#endif
}
void notified(microkit_channel ch) { (void)ch; }
