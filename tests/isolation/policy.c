/* Test-only wrapper: ordinary policy code plus a dedicated owned canary page. */
#define init policy_init
#include "../../servers/policy.c"
#undef init
#include "cases.h"
#ifndef TCS_RELEASE_PROFILE
#error "Isolation policy wrapper is release-kernel test machinery only"
#endif
uintptr_t isolation_canary_vaddr;
void init(void)
{
    *(volatile uint64_t *)isolation_canary_vaddr = ISO_CANARY;
    policy_init();
}
