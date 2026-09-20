#ifndef TCS_ADMIN_IPC_H
#define TCS_ADMIN_IPC_H
#include "tcs/ipc.h"
#include "tcs/admin_receipt.h"
#include "tcs/boot.h"

/* Experimental version-2 private contracts; no legacy admin request accepted. */
#define TCS_ADMIN_SUBMIT UINT64_C(0x201)
#define TCS_ADMIN_EXECUTE UINT64_C(0x202)
#define TCS_ADMIN_RECEIPT UINT64_C(0x280)
#define TCS_ADMIN_ADMISSION UINT64_C(0x281)
#define TCS_TEST_BOOT_CONTEXT UINT64_C(0x301)
#define TCS_TEST_BOOT_PACKET UINT64_C(0x302)
#define TCS_TEST_BOOT_DATA UINT64_C(0x380)
#define TCS_ADMIN_PACKET_WORDS 24u
#define TCS_ADMIN_RECEIPT_WORDS 13u
#define TCS_TEST_ADMIN_COMMANDS 12u

static inline void tcs_words_from_bytes(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length / 8; ++i) {
        uint64_t word = 0;
        for (unsigned j = 0; j < 8; ++j) word |= (uint64_t)bytes[8*i+j] << (8*j);
        microkit_mr_set(i, word);
    }
}
static inline void tcs_bytes_from_words(uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length / 8; ++i) {
        uint64_t word = microkit_mr_get(i);
        for (unsigned j = 0; j < 8; ++j) bytes[8*i+j] = (uint8_t)(word >> (8*j));
    }
}
static inline void tcs_execution_words(struct tcs_admin_command c)
{
    microkit_mr_set(0, c.sequence); microkit_mr_set(1, c.expected_generation);
    microkit_mr_set(2, c.request.op); microkit_mr_set(3, c.request.subject);
    microkit_mr_set(4, c.request.object); microkit_mr_set(5, c.request.rights);
}
static inline struct tcs_admin_command tcs_execution_from_words(void)
{
    return (struct tcs_admin_command){microkit_mr_get(0), microkit_mr_get(1),
        {microkit_mr_get(2), microkit_mr_get(3), microkit_mr_get(4), microkit_mr_get(5), 0}};
}
static inline microkit_msginfo tcs_admin_receipt_reply(struct tcs_admin_receipt r)
{
    tcs_execution_words(r.command);
    microkit_mr_set(6, r.decision); microkit_mr_set(7, r.audit_ok); microkit_mr_set(8, r.applied);
    microkit_mr_set(9, r.post.state); microkit_mr_set(10, r.post.generation);
    microkit_mr_set(11, r.post.object); microkit_mr_set(12, r.post.rights);
    return microkit_msginfo_new(TCS_ADMIN_RECEIPT, TCS_ADMIN_RECEIPT_WORDS);
}
static inline bool tcs_admin_receipt_decode(microkit_msginfo m, struct tcs_admin_receipt *r)
{
    *r = (struct tcs_admin_receipt){0};
    if (microkit_msginfo_get_label(m) != TCS_ADMIN_RECEIPT ||
        microkit_msginfo_get_count(m) != TCS_ADMIN_RECEIPT_WORDS) return false;
    *r = (struct tcs_admin_receipt){tcs_execution_from_words(), microkit_mr_get(6),
        microkit_mr_get(7), microkit_mr_get(8), {TCS_OK, microkit_mr_get(9),
        microkit_mr_get(10), microkit_mr_get(11), microkit_mr_get(12)}};
    return true;
}
static inline microkit_msginfo tcs_admin_admission_reply(enum tcs_admin_status status)
{
    microkit_mr_set(0, status); return microkit_msginfo_new(TCS_ADMIN_ADMISSION, 1);
}
#endif
