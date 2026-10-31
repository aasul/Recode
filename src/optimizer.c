#include "internal.h"

static bool fits_signed32(uint64_t value)
{
    return value <= INT32_MAX || value >= UINT64_MAX - (uint64_t)INT32_MAX;
}

enum rc_status rc_optimize(struct rc_program *program, struct rc_error *error)
{
    enum rc_status status = rc_validate(program, error);
    if (status != RC_OK)
        return status;
    size_t output = 0;
    size_t i = 0;
    while (i < program->count) {
        enum rc_op op = program->insns[i].op;
        if (op == RC_NOP) {
            ++i;
            continue;
        }
        if (op != RC_ADD && op != RC_SUB) {
            program->insns[output++] = program->insns[i++];
            continue;
        }
        size_t end = i;
        uint64_t delta = 0;
        while (end < program->count) {
            struct rc_insn insn = program->insns[end];
            if (insn.op == RC_ADD)
                delta += rc_extend(insn.immediate);
            else if (insn.op == RC_SUB)
                delta -= rc_extend(insn.immediate);
            else if (insn.op != RC_NOP)
                break;
            ++end;
        }
        /* Unsigned sums preserve wrapping without signed overflow. If neither
         * ADD nor SUB can encode the delta, retain the original arithmetic. */
        if (delta == 0) {
            i = end;
        } else if (fits_signed32(delta)) {
            program->insns[output++] = (struct rc_insn){RC_ADD, (uint32_t)delta};
            i = end;
        } else if (fits_signed32(UINT64_C(0) - delta)) {
            program->insns[output++] = (struct rc_insn){RC_SUB, (uint32_t)(UINT64_C(0) - delta)};
            i = end;
        } else {
            while (i < end) {
                if (program->insns[i].op != RC_NOP)
                    program->insns[output++] = program->insns[i];
                ++i;
            }
        }
    }
    program->count = output;
    return rc_fail(error, RC_OK, 0, 0);
}
