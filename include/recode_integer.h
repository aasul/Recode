#ifndef RECODE_INTEGER_H
#define RECODE_INTEGER_H

#include "recode.h"

/* Bounded mutable-register programs, separate from the byte decoder IR. */
enum { RC_INTEGER_MAX_REGISTERS = 64, RC_INTEGER_MAX_INSTRUCTIONS = 4096 };
enum rc_integer_op {
    RC_INT_ARGUMENT, RC_INT_CONSTANT, RC_INT_ADD, RC_INT_SUB, RC_INT_MUL,
    RC_INT_AND, RC_INT_OR, RC_INT_XOR, RC_INT_SHL, RC_INT_SHR,
    RC_INT_EQ, RC_INT_NE, RC_INT_ULT, RC_INT_ULE, RC_INT_SLT, RC_INT_SLE,
    RC_INT_JUMP, RC_INT_BRANCH, RC_INT_RETURN
};
struct rc_integer_insn {
    enum rc_integer_op op;
    uint32_t dst, left, right;
    uint32_t target; /* Absolute instruction index; BRANCH takes it if left != 0. */
    uint64_t immediate; /* CONSTANT value or zero-based ARGUMENT index. */
};
struct rc_builder;
struct rc_integer_program;

/* Outputs point to NULL. 1..64 registers and 0..2 arguments. No binary ABI promise. */
enum rc_status rc_builder_create(size_t registers, size_t arguments,
                                  struct rc_builder **out, struct rc_error *error);
/* Copies one instruction. Unused fields must be zero. Forward targets are
 * checked against the finished instruction count at finalization. */
enum rc_status rc_builder_append(struct rc_builder *builder, struct rc_integer_insn insn,
                                  struct rc_error *error);
/* Validate all paths, then copy into an immutable program. Builder stays owned
 * and editable; failures leave it and *out unchanged. No instruction can be
 * unreachable or read a register undefined on any incoming path. */
enum rc_status rc_builder_finalize(const struct rc_builder *builder,
                                    struct rc_integer_program **out, struct rc_error *error);
void rc_builder_destroy(struct rc_builder *builder);
void rc_integer_program_destroy(struct rc_integer_program *program);
/* Program is immutable and safe for simultaneous readers. Caller excludes
 * destruction while readers use it. budget == 0 is unlimited. Count must match. */
enum rc_status rc_integer_interpret(const struct rc_integer_program *program,
                                     const uint64_t *arguments, size_t count, uint64_t budget,
                                     uint64_t *result, struct rc_error *error);
/* Registration copies the finalized program. TRACE rejects this representation
 * with RC_UNSUPPORTED. INTERPRET, FIXED and ADAPTIVE use the managed cache. */
enum rc_status rc_runtime_register_integer(struct rc_runtime *runtime,
                                            const struct rc_integer_program *program,
                                            rc_handle *out, struct rc_error *error);
enum rc_status rc_runtime_call_integer(struct rc_runtime *runtime, rc_handle handle,
                                        const uint64_t *arguments, size_t count,
                                        uint64_t *result, struct rc_error *error);

#endif
