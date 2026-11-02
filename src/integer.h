#ifndef RECODE_INTEGER_INTERNAL_H
#define RECODE_INTEGER_INTERNAL_H
#include "internal.h"
#include "recode_integer.h"

struct rc_integer_program {
    size_t registers, arguments, count;
    bool memory;
    struct rc_integer_insn *insns;
};
typedef uint64_t (*rc_integer_function)(uint64_t, uint64_t);
struct rc_integer_result { uint64_t value; size_t offset; };
typedef enum rc_status (*rc_integer_view_function)(uint64_t, uint64_t, const void *, size_t,
                                                  struct rc_integer_result *);
struct rc_integer_batch_context {
    const struct rc_integer_batch_item *items;
    uint64_t *results;
    size_t count, completed, offset;
};
typedef enum rc_status (*rc_integer_batch_function)(struct rc_integer_batch_context *);
static inline bool rc_integer_load(enum rc_integer_op op)
{
    return op >= RC_INT_LOAD_U8 && op <= RC_INT_LOAD_U64;
}
/* A prepared layout borrows immutable program storage through publication.
 * Only rc_integer_prepare initializes it; it is not an untrusted descriptor. */
struct rc_integer_allocation {
    int registers[RC_INTEGER_MAX_REGISTERS];
    bool skip[RC_INTEGER_MAX_INSTRUCTIONS];
    uint32_t frame;
};
struct rc_integer_plan {
    const struct rc_integer_program *program;
    struct rc_integer_allocation allocation;
    size_t offsets[RC_INTEGER_MAX_INSTRUCTIONS];
    size_t failure, length;
    size_t batch, batch_offsets[RC_INTEGER_MAX_INSTRUCTIONS];
    size_t batch_success, batch_failure, batch_finish, batch_exit;
};
static inline uint64_t rc_integer_reads(struct rc_integer_insn i)
{
    bool binary = i.op >= RC_INT_ADD && i.op <= RC_INT_SLE;
    uint64_t mask = 0;
    if (binary || rc_integer_load(i.op) || i.op == RC_INT_BRANCH || i.op == RC_INT_RETURN)
        mask = UINT64_C(1) << i.left;
    if (binary) mask |= UINT64_C(1) << i.right;
    return mask;
}
enum rc_status rc_integer_prepare(const struct rc_integer_program *program,
                                   struct rc_integer_plan *plan, struct rc_error *error);
enum rc_status rc_integer_prepare_batch(const struct rc_integer_program *program,
                                         struct rc_integer_plan *plan, struct rc_error *error);
enum rc_status rc_integer_map_prepared(struct rc_integer_plan *plan, size_t mapped,
                                        struct rc_code *code, rc_integer_function *function,
                                        bool *allocated, struct rc_error *error);
enum rc_status rc_integer_validate(const struct rc_integer_program *program, struct rc_error *error);
enum rc_status rc_integer_copy(const struct rc_integer_program *program,
                                struct rc_integer_program **out, struct rc_error *error);
enum rc_status rc_integer_emit(const struct rc_integer_program *program, uint8_t *bytes,
                                size_t capacity, size_t *length, struct rc_error *error);
enum rc_status rc_integer_map(const struct rc_integer_program *program, size_t mapped,
                               struct rc_code *code, rc_integer_function *function,
                               bool *allocated, struct rc_error *error);
enum rc_status rc_integer_interpret_validated(const struct rc_integer_program *program,
                                               const uint64_t *arguments,
                                               struct rc_memory_view memory, uint64_t budget,
                                               uint64_t *result, struct rc_error *error);
#endif
