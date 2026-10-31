#include "internal.h"

#include <string.h>

static size_t encoded_size(enum rc_op op)
{
    switch (op) {
    case RC_INPUT: return 3;
    case RC_CONST: return 5;
    case RC_ADD:
    case RC_SUB: return 6;
    case RC_NOP:
    case RC_RET: return 1;
    }
    return 0;
}

enum rc_status rc_emit(const struct rc_program *program, uint8_t *bytes,
                       size_t capacity, size_t *length, struct rc_error *error)
{
    if (length == NULL || (bytes == NULL && capacity != 0))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    *length = 0;
    enum rc_status status = rc_validate(program, error);
    if (status != RC_OK)
        return status;
    /* ENDBR64 is a no-op without CET and permits indirect entry with IBT. */
    size_t needed = 4;
    for (size_t i = 0; i < program->count; ++i) {
        size_t size = encoded_size(program->insns[i].op);
        if (size > SIZE_MAX - needed)
            return rc_fail(error, RC_MEMORY, i, 0);
        needed += size;
    }
    *length = needed;
    if (bytes == NULL)
        return rc_fail(error, RC_OK, 0, 0);
    if (capacity < needed)
        return rc_fail(error, RC_CAPACITY, 0, 0);
    const uint8_t entry[] = {0xf3, 0x0f, 0x1e, 0xfa};
    memcpy(bytes, entry, sizeof(entry));
    size_t offset = sizeof(entry);
    for (size_t i = 0; i < program->count; ++i) {
        struct rc_insn insn = program->insns[i];
        switch (insn.op) {
        case RC_INPUT:
            bytes[offset++] = 0x48;
            bytes[offset++] = 0x89;
            bytes[offset++] = 0xf8;
            break;
        case RC_CONST: bytes[offset++] = 0xb8; break;
        case RC_ADD:
        case RC_SUB:
            bytes[offset++] = 0x48;
            bytes[offset++] = insn.op == RC_ADD ? 0x05 : 0x2d;
            break;
        case RC_NOP: bytes[offset++] = 0x90; break;
        case RC_RET: bytes[offset++] = 0xc3; break;
        }
        if (insn.op == RC_CONST || insn.op == RC_ADD || insn.op == RC_SUB) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes[offset++] = (uint8_t)(insn.immediate >> shift);
        }
    }
    return rc_fail(error, RC_OK, 0, 0);
}
