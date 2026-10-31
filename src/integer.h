#ifndef RECODE_INTEGER_INTERNAL_H
#define RECODE_INTEGER_INTERNAL_H
#include "internal.h"
#include "recode_integer.h"

struct rc_integer_program {
    size_t registers, arguments, count;
    struct rc_integer_insn *insns;
};
typedef uint64_t (*rc_integer_function)(uint64_t, uint64_t);
enum rc_status rc_integer_copy(const struct rc_integer_program *program,
                                struct rc_integer_program **out, struct rc_error *error);
enum rc_status rc_integer_emit(const struct rc_integer_program *program, uint8_t *bytes,
                                size_t capacity, size_t *length, struct rc_error *error);
enum rc_status rc_integer_map(const struct rc_integer_program *program, size_t mapped,
                               struct rc_code *code, rc_integer_function *function,
                               bool *allocated, struct rc_error *error);
#endif
