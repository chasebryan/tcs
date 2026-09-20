/* Minimal host-only IPC fixture, not a replacement for the real kernel tests. */
#ifndef TCS_TEST_MICROKIT_H
#define TCS_TEST_MICROKIT_H
#define TCS_HOST_TEST 1
#include <assert.h>
#include <stdint.h>
typedef unsigned microkit_channel;
#ifdef TCS_TEST_FAULT_ROUTER
typedef unsigned microkit_child;
typedef unsigned seL4_Bool;
#define seL4_False 0u
enum { seL4_Fault_VMFault = 6, seL4_VMFault_IP = 0, seL4_VMFault_Addr = 1,
       seL4_VMFault_PrefetchFault = 2, seL4_VMFault_FSR = 3, seL4_VMFault_Length = 4 };
static void test_pd_stop(microkit_child child);
static void test_pd_resume(microkit_child child);
static inline void microkit_pd_stop(microkit_child child) { test_pd_stop(child); }
static inline void microkit_pd_resume(microkit_child child) { test_pd_resume(child); }
#endif
typedef struct { uint64_t label, count; } microkit_msginfo;
static uint64_t test_mrs[64];
static unsigned test_notifications, test_acks;
static inline uint64_t microkit_msginfo_get_label(microkit_msginfo m) { return m.label; }
static inline uint64_t microkit_msginfo_get_count(microkit_msginfo m) { return m.count; }
static inline microkit_msginfo microkit_msginfo_new(uint64_t l, uint64_t c)
{ return (microkit_msginfo){l, c}; }
static inline uint64_t microkit_mr_get(unsigned i) { return test_mrs[i]; }
static inline void microkit_mr_set(unsigned i, uint64_t v) { test_mrs[i] = v; }
static inline void microkit_notify(microkit_channel ch)
{ assert(ch == 1); ++test_notifications; }
static inline void microkit_irq_ack(microkit_channel ch)
{ assert(ch == 0); ++test_acks; }
#ifdef TCS_TEST_IPC_ROUTER
static microkit_msginfo test_ppcall(microkit_channel ch, microkit_msginfo m);
static inline microkit_msginfo microkit_ppcall(microkit_channel ch, microkit_msginfo m)
{ return test_ppcall(ch, m); }
static inline void microkit_dbg_puts(const char *s) { (void)s; }
#else
static inline microkit_msginfo microkit_ppcall(microkit_channel ch, microkit_msginfo m)
{ (void)ch; (void)m; assert(0 && "serial must never make a protected call"); return m; }
#endif
#endif
