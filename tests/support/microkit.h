/* Minimal host-only IPC fixture, not a replacement for the real kernel tests. */
#ifndef TCS_TEST_MICROKIT_H
#define TCS_TEST_MICROKIT_H
#include <assert.h>
#include <stdint.h>
typedef unsigned microkit_channel;
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
static inline microkit_msginfo microkit_ppcall(microkit_channel ch, microkit_msginfo m)
{ (void)ch; (void)m; assert(0 && "serial must never make a protected call"); return m; }
#endif
