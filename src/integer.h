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
static inline bool rc_integer_load(enum rc_integer_op op)
{
    return op >= RC_INT_LOAD_U8 && op <= RC_INT_LOAD_U64;
}
enum rc_status rc_integer_copy(const struct rc_integer_program *program,
                                struct rc_integer_program **out, struct rc_error *error);
enum rc_status rc_integer_emit(const struct rc_integer_program *program, uint8_t *bytes,
                                size_t capacity, size_t *length, struct rc_error *error);
enum rc_status rc_integer_map(const struct rc_integer_program *program, size_t mapped,
                               struct rc_code *code, rc_integer_function *function,
                               bool *allocated, struct rc_error *error);
#endif
