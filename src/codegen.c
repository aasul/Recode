#include "internal.h"

#include <string.h>

static size_t encoded_size(enum rc_op op)
{
    switch (op) {
    case RC_INPUT:
    case RC_TEST: return 3;
    case RC_CONST:
    case RC_JMP: return 5;
    case RC_ADD:
    case RC_SUB:
    case RC_CMP:
    case RC_JZ:
    case RC_JNZ: return 6;
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
    struct rc_cfg cfg = {0};
    enum rc_status status = rc_analyze(program, &cfg, error);
    if (status != RC_OK)
        return status;
    /* ENDBR64 is a no-op without CET and permits indirect entry with IBT. */
    size_t needed = 4;
    for (size_t i = 0; i < program->count; ++i) {
        size_t size = encoded_size(program->insns[i].op);
        cfg.block_for[i] = needed;
        if (size > SIZE_MAX - needed) {
            rc_cfg_free(&cfg);
            return rc_fail(error, RC_MEMORY, i, 0);
        }
        needed += size;
    }
    for (size_t i = 0; i < program->count; ++i) {
        struct rc_insn insn = program->insns[i];
        if (!rc_is_branch(insn.op))
            continue;
        size_t next = cfg.block_for[i] + encoded_size(insn.op);
        size_t target = cfg.block_for[insn.immediate];
        if ((target >= next && target - next > INT32_MAX) ||
            (target < next && next - target > (size_t)INT32_MAX + 1)) {
            rc_cfg_free(&cfg);
            return rc_fail(error, RC_CAPACITY, i, 0);
        }
    }
    *length = needed;
    if (bytes == NULL || capacity < needed) {
        rc_cfg_free(&cfg);
        return rc_fail(error, bytes == NULL ? RC_OK : RC_CAPACITY, 0, 0);
    }
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
        case RC_TEST:
            bytes[offset++] = 0x48;
            bytes[offset++] = 0x85;
            bytes[offset++] = 0xc0;
            break;
        case RC_CONST: bytes[offset++] = 0xb8; break;
        case RC_ADD:
        case RC_SUB:
        case RC_CMP:
            bytes[offset++] = 0x48;
            bytes[offset++] = insn.op == RC_ADD ? 0x05 : (insn.op == RC_SUB ? 0x2d : 0x3d);
            break;
        case RC_JMP: bytes[offset++] = 0xe9; break;
        case RC_JZ:
        case RC_JNZ:
            bytes[offset++] = 0x0f;
            bytes[offset++] = insn.op == RC_JZ ? 0x84 : 0x85;
            break;
        case RC_NOP: bytes[offset++] = 0x90; break;
        case RC_RET: bytes[offset++] = 0xc3; break;
        }
        uint32_t immediate = insn.immediate;
        if (rc_is_branch(insn.op)) {
            size_t target = cfg.block_for[immediate];
            size_t next = offset + 4;
            immediate = target >= next ? (uint32_t)(target - next)
                : UINT32_C(0) - (uint32_t)(next - target);
        }
        if (insn.op == RC_CONST || insn.op == RC_ADD || insn.op == RC_SUB ||
            insn.op == RC_CMP || rc_is_branch(insn.op)) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes[offset++] = (uint8_t)(immediate >> shift);
        }
    }
    rc_cfg_free(&cfg);
    return rc_fail(error, RC_OK, 0, 0);
}
