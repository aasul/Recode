#ifndef RECODE_H
#define RECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum rc_status {
    RC_OK, RC_ARGUMENT, RC_MEMORY, RC_TRUNCATED, RC_UNSUPPORTED,
    RC_UNINITIALIZED, RC_MISSING_RET, RC_TRAILING, RC_INVALID_IR,
    RC_CAPACITY, RC_SYSTEM, RC_BRANCH, RC_UNDEFINED_ZF, RC_BUSY, RC_NOT_FOUND, RC_BUDGET
};

struct rc_error {
    enum rc_status status;
    size_t offset; /* Byte offset for decoding, instruction index for IR errors. */
    int system_errno;
};

enum rc_op {
    RC_INPUT, RC_CONST, RC_ADD, RC_SUB, RC_NOP, RC_RET,
    RC_CMP, RC_TEST, RC_JMP, RC_JZ, RC_JNZ
};

struct rc_insn {
    enum rc_op op;
    /* CONST zero-extends; ADD/SUB/CMP sign-extend raw imm32 bits.
     * JMP/JZ/JNZ instead store an absolute instruction index. */
    uint32_t immediate;
};

struct rc_program {
    struct rc_insn *insns;
    size_t count;
};

typedef uint64_t (*rc_function)(uint64_t);
struct rc_code {
    void *mapping;
    size_t size;
    rc_function function;
};

const char *rc_status_string(enum rc_status status);
/* Decode and compile outputs must be zero-initialized and empty. Nonempty
 * outputs are rejected unchanged. error may be NULL; storage must not overlap. */
enum rc_status rc_decode(const uint8_t *bytes, size_t length,
                         struct rc_program *program, struct rc_error *error);
/* Release a decoded allocation or an empty object, never a borrowed IR array. */
void rc_program_free(struct rc_program *program);
enum rc_status rc_validate(const struct rc_program *program, struct rc_error *error);
enum rc_status rc_optimize(struct rc_program *program, struct rc_error *error);
/* Query size with bytes == NULL and capacity == 0. Insufficient capacity writes nothing. */
enum rc_status rc_emit(const struct rc_program *program, uint8_t *bytes,
                       size_t capacity, size_t *length, struct rc_error *error);
/* Failed rollback retains a mapping with function == NULL; release it with
 * rc_code_free. No executable function is returned on compilation failure. */
enum rc_status rc_compile(const uint8_t *bytes, size_t length, bool optimize,
                          struct rc_code *code, struct rc_error *error);
/* No calls may be in flight when releasing code. On failure the mapping stays owned. */
enum rc_status rc_code_free(struct rc_code *code, struct rc_error *error);

/* budget == 0 means unlimited. A positive budget counts executed IR instructions.
 * Invalid programs and exhausted budgets leave result unchanged. */
enum rc_status rc_interpret(const struct rc_program *program, uint64_t argument,
                             uint64_t budget, uint64_t *result, struct rc_error *error);

#endif
