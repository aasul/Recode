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
    case 0xeb:
    case 0x74:
    case 0x75:
        if (remaining < 2)
            return RC_TRUNCATED;
        insn->op = bytes[0] == 0xeb ? RC_JMP : (bytes[0] == 0x74 ? RC_JZ : RC_JNZ);
        insn->immediate = bytes[1] < 128 ? bytes[1] : UINT32_C(0xffffff00) | bytes[1];
        *length = 2;
        return RC_OK;
    case 0xe9:
        if (remaining < 5)
            return RC_TRUNCATED;
        insn->op = RC_JMP;
        insn->immediate = read_u32(bytes + 1);
        *length = 5;
        return RC_OK;
    case 0x0f:
        if (remaining < 2)
            return RC_TRUNCATED;
        if (bytes[1] != 0x84 && bytes[1] != 0x85)
            return RC_UNSUPPORTED;
        if (remaining < 6)
            return RC_TRUNCATED;
        insn->op = bytes[1] == 0x84 ? RC_JZ : RC_JNZ;
        insn->immediate = read_u32(bytes + 2);
        *length = 6;
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
    if (bytes[1] == 0x05 || bytes[1] == 0x2d || bytes[1] == 0x3d) {
        if (remaining < 6)
            return RC_TRUNCATED;
        insn->op = bytes[1] == 0x05 ? RC_ADD : (bytes[1] == 0x2d ? RC_SUB : RC_CMP);
        insn->immediate = read_u32(bytes + 2);
        *length = 6;
        return RC_OK;
    }
    if (bytes[1] == 0x81 || bytes[1] == 0x83) {
        if (remaining < 3)
            return RC_TRUNCATED;
        if (bytes[2] != 0xc0 && bytes[2] != 0xe8 &&
            !(bytes[1] == 0x81 && bytes[2] == 0xf8))
            return RC_UNSUPPORTED;
        *length = bytes[1] == 0x81 ? 7 : 4;
        if (remaining < *length)
            return RC_TRUNCATED;
        insn->op = bytes[2] == 0xc0 ? RC_ADD : (bytes[2] == 0xe8 ? RC_SUB : RC_CMP);
        if (bytes[1] == 0x81)
            insn->immediate = read_u32(bytes + 3);
        else {
            uint32_t value = bytes[3];
            insn->immediate = value < 128 ? value : value | UINT32_C(0xffffff00);
        }
        return RC_OK;
    }
    if (bytes[1] == 0x85) {
        if (remaining < 3)
            return RC_TRUNCATED;
        if (bytes[2] != 0xc0)
            return RC_UNSUPPORTED;
        insn->op = RC_TEST;
        *length = 3;
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
    if (length > UINT32_MAX || length > SIZE_MAX / sizeof(struct rc_insn) ||
        length >= SIZE_MAX / sizeof(size_t))
        return rc_fail(error, RC_MEMORY, 0, 0);
    struct rc_program decoded = {malloc(length * sizeof(struct rc_insn)), 0};
    if (decoded.insns == NULL)
        return rc_fail(error, RC_MEMORY, 0, 0);
    size_t *offsets = malloc((length + 1) * sizeof(*offsets));
    if (offsets == NULL) {
        rc_program_free(&decoded);
        return rc_fail(error, RC_MEMORY, 0, 0);
    }
    size_t offset = 0;
    enum rc_status status = RC_OK;
    while (offset < length) {
        struct rc_insn insn;
        size_t used;
        status = decode_one(bytes + offset, length - offset, &insn, &used);
        if (status != RC_OK)
            goto failed;
        offsets[decoded.count] = offset;
        decoded.insns[decoded.count++] = insn;
        offset += used;
    }
    offsets[decoded.count] = length;
    for (size_t i = 0; i < decoded.count; ++i) {
        struct rc_insn *insn = &decoded.insns[i];
        if (!rc_is_branch(insn->op))
            continue;
        uint32_t bits = insn->immediate;
        int64_t displacement = bits;
        if (bits > INT32_MAX)
            displacement -= INT64_C(4294967296);
        size_t next = offsets[i + 1];
        offset = offsets[i];
        status = RC_BRANCH;
        size_t target;
        if (displacement < 0) {
            size_t distance = (size_t)(-displacement);
            if (distance > next)
                goto failed;
            target = next - distance;
        } else {
            size_t distance = (size_t)displacement;
            if (distance >= length - next)
                goto failed;
            target = next + distance;
        }
        size_t low = 0, high = decoded.count;
        while (low < high) {
            size_t mid = low + (high - low) / 2;
            if (offsets[mid] < target)
                low = mid + 1;
            else
                high = mid;
        }
        if (low == decoded.count || offsets[low] != target)
            goto failed;
        insn->immediate = (uint32_t)low;
    }
    struct rc_error validation;
    status = rc_validate(&decoded, &validation);
    if (status != RC_OK) {
        offset = offsets[validation.offset];
        goto failed;
    }
    free(offsets);
    *program = decoded;
    return rc_fail(error, RC_OK, 0, 0);
failed:
    free(offsets);
    rc_program_free(&decoded);
    return rc_fail(error, status, offset, 0);
}
