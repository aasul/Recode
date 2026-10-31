#include "trace.h"

/* Validated immutable IR is kept alive by the runtime's active-call ownership. */
static bool step(const struct rc_program *program, struct rc_state *s)
{
    struct rc_insn insn = program->insns[s->pc++];
    switch (insn.op) {
    case RC_INPUT: s->rax = s->argument; break;
    case RC_CONST: s->rax = insn.immediate; break;
    case RC_ADD: s->rax += rc_extend(insn.immediate); s->zf = s->rax == 0; break;
    case RC_SUB: s->rax -= rc_extend(insn.immediate); s->zf = s->rax == 0; break;
    case RC_CMP: s->zf = s->rax == rc_extend(insn.immediate); break;
    case RC_TEST: s->zf = s->rax == 0; break;
    case RC_JMP: s->pc = insn.immediate; break;
    case RC_JZ: if (s->zf != 0) s->pc = insn.immediate; break;
    case RC_JNZ: if (s->zf == 0) s->pc = insn.immediate; break;
    case RC_NOP: break;
    case RC_RET: return true;
    }
    return false;
}

static void finish(const struct rc_program *program, struct rc_state *state)
{
    while (!step(program,state)) { }
}

enum rc_status rc_interpret_validated(const struct rc_program *program, uint64_t argument,
                                       uint64_t budget, uint64_t *result)
{
    struct rc_state state = {.argument = argument};
    if (budget == 0) {
        finish(program,&state);
        *result = state.rax;
        return RC_OK;
    }
    for (;;) {
        if (budget == 0) return RC_BUDGET;
        --budget;
        if (step(program,&state)) {
            *result = state.rax;
            return RC_OK;
        }
    }
}

void rc_interpret_observed(const struct rc_program *program, struct rc_state *s,
                            struct rc_observer *o)
{
    for (;;) {
        if (o->before == NULL) {
            finish(program,s);
            return;
        }
        if (o->recording == NULL && o->before(o,s))
            return;
        size_t pc = s->pc;
        bool returned = step(program,s);
        if (o->recording != NULL)
            rc_trace_record(o->recording,program,pc,s,returned);
        o->after(o,pc,s,returned);
        if (returned) return;
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
