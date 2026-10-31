#define _GNU_SOURCE
#include "integer.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

/* Fixed slots make every rel32 target independent of instruction encoding.
 * RAX/RCX are scratch, RDI/RSI retain the two arguments. No calls or callee-saved
 * clobbers; at most 512 stack bytes, restored on every RETURN. */
enum { ENTRY_SIZE = 11, SLOT_SIZE = 40 };
struct output { uint8_t *bytes; size_t count; };
static void byte(struct output *o, uint8_t value) { o->bytes[o->count++] = value; }
static void word(struct output *o, uint32_t value)
{
    for (unsigned n = 0; n < 32; n += 8) byte(o, (uint8_t)(value >> n));
}
static void stack(struct output *o, uint8_t opcode, uint8_t modrm, uint32_t reg)
{
    byte(o, 0x48); byte(o, opcode); byte(o, modrm); byte(o, 0x24); word(o, reg * 8);
}
static void frame(struct output *o, uint8_t modrm, uint32_t size)
{
    byte(o, 0x48); byte(o, 0x81); byte(o, modrm); word(o, size);
}

enum rc_status rc_integer_emit(const struct rc_integer_program *p, uint8_t *bytes,
                                size_t capacity, size_t *length, struct rc_error *error)
{
    if (p == NULL || length == NULL || (bytes == NULL && capacity != 0))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    size_t needed = ENTRY_SIZE + p->count * SLOT_SIZE;
    *length = needed;
    if (bytes == NULL) return rc_fail(error, RC_OK, 0, 0);
    if (capacity < needed) return rc_fail(error, RC_CAPACITY, 0, 0);
    memset(bytes, 0x90, needed);
    struct output o = {bytes, 0};
    byte(&o, 0xf3); byte(&o, 0x0f); byte(&o, 0x1e); byte(&o, 0xfa);
    uint32_t size = (uint32_t)((p->registers * 8 + 15) / 16 * 16);
    frame(&o, 0xec, size); /* SUB RSP, imm32 */
    for (size_t n = 0; n < p->count; ++n) {
        struct rc_integer_insn i = p->insns[n];
        o.count = ENTRY_SIZE + n * SLOT_SIZE;
        if (i.op >= RC_INT_ADD && i.op <= RC_INT_SLE) {
            stack(&o, 0x8b, 0x84, i.left); /* MOV RAX,[RSP+disp32] */
            stack(&o, 0x8b, 0x8c, i.right); /* MOV RCX,[RSP+disp32] */
        }
        switch (i.op) {
        case RC_INT_ARGUMENT:
            stack(&o, 0x89, i.immediate == 0 ? 0xbc : 0xb4, i.dst);
            continue;
        case RC_INT_CONSTANT:
            byte(&o, 0x48); byte(&o, 0xb8);
            word(&o, (uint32_t)i.immediate); word(&o, (uint32_t)(i.immediate >> 32));
            break;
        case RC_INT_ADD: byte(&o, 0x48); byte(&o, 0x01); byte(&o, 0xc8); break;
        case RC_INT_SUB: byte(&o, 0x48); byte(&o, 0x29); byte(&o, 0xc8); break;
        case RC_INT_MUL: byte(&o, 0x48); byte(&o, 0x0f); byte(&o, 0xaf); byte(&o, 0xc1); break;
        case RC_INT_AND: byte(&o, 0x48); byte(&o, 0x21); byte(&o, 0xc8); break;
        case RC_INT_OR: byte(&o, 0x48); byte(&o, 0x09); byte(&o, 0xc8); break;
        case RC_INT_XOR: byte(&o, 0x48); byte(&o, 0x31); byte(&o, 0xc8); break;
        case RC_INT_SHL:
        case RC_INT_SHR:
            byte(&o, 0x48); byte(&o, 0xd3); byte(&o, i.op == RC_INT_SHL ? 0xe0 : 0xe8);
            break;
        case RC_INT_EQ:
        case RC_INT_NE:
        case RC_INT_ULT:
        case RC_INT_ULE:
        case RC_INT_SLT:
        case RC_INT_SLE: {
            const uint8_t conditions[] = {0x94, 0x95, 0x92, 0x96, 0x9c, 0x9e};
            byte(&o, 0x48); byte(&o, 0x39); byte(&o, 0xc8); /* CMP RAX,RCX */
            byte(&o, 0x0f); byte(&o, conditions[i.op - RC_INT_EQ]); byte(&o, 0xc0);
            byte(&o, 0x0f); byte(&o, 0xb6); byte(&o, 0xc0); /* MOVZX EAX,AL */
            break;
        }
        case RC_INT_BRANCH:
            stack(&o, 0x8b, 0x84, i.left);
            byte(&o, 0x48); byte(&o, 0x85); byte(&o, 0xc0);
            byte(&o, 0x0f); byte(&o, 0x85);
            word(&o, (uint32_t)(ENTRY_SIZE + i.target * SLOT_SIZE) - (uint32_t)(o.count + 4));
            continue;
        case RC_INT_JUMP:
            byte(&o, 0xe9);
            word(&o, (uint32_t)(ENTRY_SIZE + i.target * SLOT_SIZE) - (uint32_t)(o.count + 4));
            continue;
        case RC_INT_RETURN:
            stack(&o, 0x8b, 0x84, i.left);
            frame(&o, 0xc4, size); /* ADD RSP, imm32 */
            byte(&o, 0xc3);
            continue;
        }
        stack(&o, 0x89, 0x84, i.dst);
    }
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_integer_map(const struct rc_integer_program *p, size_t mapped,
                               struct rc_code *code, rc_integer_function *function,
                               bool *allocated, struct rc_error *error)
{
    *allocated = false;
    *function = NULL;
    void *mapping = mmap(NULL, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return rc_fail(error, RC_SYSTEM, 0, errno);
    *allocated = true;
    *code = (struct rc_code){mapping, mapped, NULL};
    size_t emitted;
    enum rc_status status = rc_integer_emit(p, mapping, mapped, &emitted, error);
    if (status == RC_OK && mprotect(mapping, mapped, PROT_READ | PROT_EXEC) != 0)
        status = rc_fail(error, RC_SYSTEM, 0, errno);
    if (status != RC_OK) {
        struct rc_error cleanup;
        if (rc_code_free(code, &cleanup) != RC_OK) {
            if (error != NULL) *error = cleanup;
            return RC_SYSTEM;
        }
        return status;
    }
    _Static_assert(sizeof(*function) == sizeof(mapping), "System V function pointer size");
    memcpy(function, &mapping, sizeof(*function));
    return rc_fail(error, RC_OK, 0, 0);
}
