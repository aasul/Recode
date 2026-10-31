#include "internal.h"

#include <stdlib.h>

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static enum rc_status decode_one(const uint8_t *bytes, size_t remaining,
                                 struct rc_insn *insn, size_t *length)
{
    *insn = (struct rc_insn){RC_NOP, 0};
    *length = 1;
    switch (bytes[0]) {
    case 0x90: return RC_OK;
    case 0xc3: insn->op = RC_RET; return RC_OK;
    case 0xb8:
        if (remaining < 5)
            return RC_TRUNCATED;
        insn->op = RC_CONST;
        insn->immediate = read_u32(bytes + 1);
        *length = 5;
        return RC_OK;
    case 0x48:
        break;
    default: return RC_UNSUPPORTED;
    }
    if (remaining < 2)
        return RC_TRUNCATED;
    if (bytes[1] == 0x89 || bytes[1] == 0x8b) {
        if (remaining < 3)
            return RC_TRUNCATED;
        if (bytes[2] != (bytes[1] == 0x89 ? 0xf8 : 0xc7))
            return RC_UNSUPPORTED;
        insn->op = RC_INPUT;
        *length = 3;
        return RC_OK;
    }
    if (bytes[1] == 0x05 || bytes[1] == 0x2d) {
        if (remaining < 6)
            return RC_TRUNCATED;
        insn->op = bytes[1] == 0x05 ? RC_ADD : RC_SUB;
        insn->immediate = read_u32(bytes + 2);
        *length = 6;
        return RC_OK;
    }
    if (bytes[1] == 0x81 || bytes[1] == 0x83) {
        if (remaining < 3)
            return RC_TRUNCATED;
        if (bytes[2] != 0xc0 && bytes[2] != 0xe8)
            return RC_UNSUPPORTED;
        *length = bytes[1] == 0x81 ? 7 : 4;
        if (remaining < *length)
            return RC_TRUNCATED;
        insn->op = bytes[2] == 0xc0 ? RC_ADD : RC_SUB;
        if (bytes[1] == 0x81)
            insn->immediate = read_u32(bytes + 3);
        else {
            uint32_t value = bytes[3];
            insn->immediate = value < 128 ? value : value | UINT32_C(0xffffff00);
        }
        return RC_OK;
    }
    return RC_UNSUPPORTED;
}

enum rc_status rc_decode(const uint8_t *bytes, size_t length,
                         struct rc_program *program, struct rc_error *error)
{
    if (program == NULL || program->insns != NULL || program->count != 0 ||
        (bytes == NULL && length != 0))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    if (length == 0)
        return rc_fail(error, RC_MISSING_RET, 0, 0);
    if (length > SIZE_MAX / sizeof(struct rc_insn))
        return rc_fail(error, RC_MEMORY, 0, 0);
    struct rc_program decoded = {malloc(length * sizeof(struct rc_insn)), 0};
    if (decoded.insns == NULL)
        return rc_fail(error, RC_MEMORY, 0, 0);
    bool initialized = false;
    size_t offset = 0;
    while (offset < length) {
        struct rc_insn insn;
        size_t used;
        enum rc_status status = decode_one(bytes + offset, length - offset, &insn, &used);
        if (status == RC_OK) {
            if (insn.op == RC_INPUT || insn.op == RC_CONST)
                initialized = true;
            else if (insn.op != RC_NOP && !initialized)
                status = RC_UNINITIALIZED;
        }
        if (status == RC_OK && insn.op == RC_RET && used != length - offset)
            status = RC_TRAILING;
        if (status != RC_OK) {
            rc_program_free(&decoded);
            return rc_fail(error, status, offset, 0);
        }
        decoded.insns[decoded.count++] = insn;
        offset += used;
        if (insn.op == RC_RET) {
            *program = decoded;
            return rc_fail(error, RC_OK, 0, 0);
        }
    }
    rc_program_free(&decoded);
    return rc_fail(error, RC_MISSING_RET, offset, 0);
}
