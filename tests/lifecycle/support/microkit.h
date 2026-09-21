#ifndef TCS_TEST_MICROKIT_H
#define TCS_TEST_MICROKIT_H
#define TCS_HOST_TEST 1
#include <stdint.h>
typedef unsigned microkit_channel;
typedef unsigned microkit_child;
typedef unsigned seL4_Bool;
#define seL4_False 0u
enum { seL4_Fault_VMFault = 6, seL4_VMFault_IP = 0, seL4_VMFault_Addr = 1,
    seL4_VMFault_PrefetchFault = 2, seL4_VMFault_FSR = 3, seL4_VMFault_Length = 4 };
typedef struct { uint64_t label, count; } microkit_msginfo;
static uint64_t lc_mrs[64];
static microkit_msginfo lc_route(microkit_channel ch, microkit_msginfo msg);
static void lc_notify(microkit_channel ch);
static void lc_stop(microkit_child child);
static void lc_resume(microkit_child child);
static inline uint64_t microkit_msginfo_get_label(microkit_msginfo m) { return m.label; }
static inline uint64_t microkit_msginfo_get_count(microkit_msginfo m) { return m.count; }
static inline microkit_msginfo microkit_msginfo_new(uint64_t l, uint64_t c) { return (microkit_msginfo){l, c}; }
static inline void microkit_mr_set(unsigned i, uint64_t n) { lc_mrs[i] = n; }
static inline uint64_t microkit_mr_get(unsigned i) { return lc_mrs[i]; }
static inline microkit_msginfo microkit_ppcall(microkit_channel ch, microkit_msginfo m) { return lc_route(ch, m); }
static inline void microkit_notify(microkit_channel ch) { lc_notify(ch); }
static inline void microkit_pd_stop(microkit_child child) { lc_stop(child); }
static inline void microkit_pd_resume(microkit_child child) { lc_resume(child); }
#endif
