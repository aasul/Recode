#include "internal.h"

/* The caller has validated immutable storage for the duration of execution. */
enum rc_status rc_interpret_validated(const struct rc_program *program, uint64_t argument,
                                       uint64_t budget, uint64_t *result)
{
    uint64_t rax = 0;
    bool zero = false;
    size_t pc = 0;
    bool limited = budget != 0;
    for (;;) {
        if (limited) {
            if (budget == 0)
                return RC_BUDGET;
            --budget;
        }
        struct rc_insn insn = program->insns[pc++];
        switch (insn.op) {
        case RC_INPUT: rax = argument; break;
        case RC_CONST: rax = insn.immediate; break;
        case RC_ADD: rax += rc_extend(insn.immediate); zero = rax == 0; break;
        case RC_SUB: rax -= rc_extend(insn.immediate); zero = rax == 0; break;
        case RC_CMP: zero = rax == rc_extend(insn.immediate); break;
        case RC_TEST: zero = rax == 0; break;
        case RC_JMP: pc = insn.immediate; break;
        case RC_JZ: if (zero) pc = insn.immediate; break;
        case RC_JNZ: if (!zero) pc = insn.immediate; break;
        case RC_NOP: break;
        case RC_RET: *result = rax; return RC_OK;
        }
    }
}

enum rc_status rc_interpret(const struct rc_program *program, uint64_t argument,
                             uint64_t budget, uint64_t *result, struct rc_error *error)
{
    if (result == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    enum rc_status status = rc_validate(program, error);
    if (status != RC_OK)
        return status;
    status = rc_interpret_validated(program, argument, budget, result);
    return rc_fail(error, status, 0, 0);
}
