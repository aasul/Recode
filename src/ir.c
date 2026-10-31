#include "internal.h"

#include <stdlib.h>

const char *rc_status_string(enum rc_status status)
{
    switch (status) {
    case RC_OK: return "success";
    case RC_ARGUMENT: return "invalid argument or nonempty output";
    case RC_MEMORY: return "allocation failed or size overflow";
    case RC_TRUNCATED: return "truncated instruction";
    case RC_UNSUPPORTED: return "unsupported encoding";
    case RC_UNINITIALIZED: return "RAX used before initialization";
    case RC_MISSING_RET: return "missing final ret";
    case RC_TRAILING: return "bytes or instructions after ret";
    case RC_INVALID_IR: return "invalid IR operation";
    case RC_CAPACITY: return "output buffer too small";
    case RC_SYSTEM: return "operating system error";
    }
    return "unknown error";
}

void rc_program_free(struct rc_program *program)
{
    if (program != NULL) {
        free(program->insns);
        *program = (struct rc_program){0};
    }
}

enum rc_status rc_validate(const struct rc_program *program, struct rc_error *error)
{
    bool initialized = false;
    if (program == NULL || (program->count != 0 && program->insns == NULL))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    for (size_t i = 0; i < program->count; ++i) {
        switch (program->insns[i].op) {
        case RC_INPUT:
        case RC_CONST:
            initialized = true;
            break;
        case RC_ADD:
        case RC_SUB:
        case RC_RET:
            if (!initialized)
                return rc_fail(error, RC_UNINITIALIZED, i, 0);
            if (program->insns[i].op == RC_RET) {
                if (i != program->count - 1)
                    return rc_fail(error, RC_TRAILING, i + 1, 0);
                return rc_fail(error, RC_OK, 0, 0);
            }
            break;
        case RC_NOP:
            break;
        default:
            return rc_fail(error, RC_INVALID_IR, i, 0);
        }
    }
    return rc_fail(error, RC_MISSING_RET, program->count, 0);
}
