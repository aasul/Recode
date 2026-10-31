#include "integer.h"

#include <stdlib.h>
#include <string.h>

struct rc_builder { struct rc_integer_program program; size_t capacity; };

static enum rc_status check_insn(const struct rc_integer_program *p,
                                  struct rc_integer_insn i)
{
    if (i.op < RC_INT_ARGUMENT || i.op > RC_INT_RETURN)
        return RC_INVALID_IR;
    bool binary = i.op >= RC_INT_ADD && i.op <= RC_INT_SLE;
    bool writes = i.op <= RC_INT_SLE;
    bool reads = binary || i.op == RC_INT_BRANCH || i.op == RC_INT_RETURN;
    bool branch = i.op == RC_INT_JUMP || i.op == RC_INT_BRANCH;
    if ((writes ? i.dst >= p->registers : i.dst != 0) ||
        (reads ? i.left >= p->registers : i.left != 0) ||
        (binary ? i.right >= p->registers : i.right != 0) ||
        (branch ? i.target >= RC_INTEGER_MAX_INSTRUCTIONS : i.target != 0) ||
        (i.op == RC_INT_ARGUMENT && i.immediate >= p->arguments) ||
        (i.op != RC_INT_ARGUMENT && i.op != RC_INT_CONSTANT && i.immediate != 0))
        return RC_INVALID_IR;
    return RC_OK;
}

enum rc_status rc_builder_create(size_t registers, size_t arguments,
                                  struct rc_builder **out, struct rc_error *error)
{
    if (out == NULL || *out != NULL || registers == 0 ||
        registers > RC_INTEGER_MAX_REGISTERS || arguments > 2)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    struct rc_builder *b = malloc(sizeof(*b));
    if (b == NULL) return rc_fail(error, RC_MEMORY, 0, 0);
    *b = (struct rc_builder){.program = {.registers = registers, .arguments = arguments}};
    *out = b;
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_builder_append(struct rc_builder *b, struct rc_integer_insn insn,
                                  struct rc_error *error)
{
    if (b == NULL) return rc_fail(error, RC_ARGUMENT, 0, 0);
    enum rc_status status = check_insn(&b->program, insn);
    if (status != RC_OK) return rc_fail(error, status, b->program.count, 0);
    if (b->program.count == RC_INTEGER_MAX_INSTRUCTIONS)
        return rc_fail(error, RC_CAPACITY, b->program.count, 0);
    if (b->program.count == b->capacity) {
        size_t capacity = b->capacity == 0 ? 16 : b->capacity * 2;
        struct rc_integer_insn *insns = malloc(capacity * sizeof(*insns));
        if (insns == NULL) return rc_fail(error, RC_MEMORY, b->program.count, 0);
        if (b->program.count != 0)
            memcpy(insns, b->program.insns, b->program.count * sizeof(*insns));
        free(b->program.insns);
        b->program.insns = insns;
        b->capacity = capacity;
    }
    b->program.insns[b->program.count++] = insn;
    return rc_fail(error, RC_OK, 0, 0);
}

/* One instruction per node keeps branch validation and must analysis explicit.
 * Each fact drops at most 64 bits. The external entry always has no definitions. */
static enum rc_status validate(const struct rc_integer_program *p, struct rc_error *error)
{
    if (p->count == 0) return rc_fail(error, RC_MISSING_RET, 0, 0);
    struct facts { uint64_t defined; bool reachable; };
    struct facts *facts = malloc(p->count * sizeof(*facts));
    if (facts == NULL) return rc_fail(error, RC_MEMORY, 0, 0);
    enum rc_status status = RC_OK;
    size_t offset = 0;
    for (size_t n = 0; n < p->count; ++n) {
        struct rc_integer_insn i = p->insns[n];
        offset = n;
        status = check_insn(p, i);
        if (status != RC_OK) goto done;
        if ((i.op == RC_INT_JUMP || i.op == RC_INT_BRANCH) && i.target >= p->count) {
            status = RC_INVALID_IR; goto done;
        }
        if (n + 1 == p->count && i.op != RC_INT_JUMP && i.op != RC_INT_RETURN) {
            status = RC_MISSING_RET; goto done;
        }
        facts[n] = (struct facts){UINT64_MAX, false};
    }
    facts[0] = (struct facts){0, true};
    bool changed;
    do {
        changed = false;
        for (size_t n = 0; n < p->count; ++n) {
            if (!facts[n].reachable) continue;
            struct rc_integer_insn i = p->insns[n];
            uint64_t defined = facts[n].defined;
            if (i.op <= RC_INT_SLE) defined |= UINT64_C(1) << i.dst;
            size_t edges[2], count = 0;
            if (i.op == RC_INT_JUMP || i.op == RC_INT_BRANCH) edges[count++] = i.target;
            if (i.op != RC_INT_JUMP && i.op != RC_INT_RETURN) edges[count++] = n + 1;
            for (size_t e = 0; e < count; ++e) {
                struct facts *next = &facts[edges[e]];
                uint64_t merged = next->defined & defined;
                if (!next->reachable || merged != next->defined) {
                    next->reachable = true;
                    next->defined = merged;
                    changed = true;
                }
            }
        }
    } while (changed);
    for (size_t n = 0; n < p->count; ++n) {
        offset = n;
        if (!facts[n].reachable) { status = RC_TRAILING; goto done; }
        struct rc_integer_insn i = p->insns[n];
        bool binary = i.op >= RC_INT_ADD && i.op <= RC_INT_SLE;
        uint64_t reads = 0;
        if (binary || i.op == RC_INT_BRANCH || i.op == RC_INT_RETURN)
            reads |= UINT64_C(1) << i.left;
        if (binary) reads |= UINT64_C(1) << i.right;
        if ((reads & facts[n].defined) != reads) { status = RC_UNINITIALIZED; goto done; }
    }
done:
    free(facts);
    return rc_fail(error, status, status == RC_OK ? 0 : offset, 0);
}

enum rc_status rc_integer_copy(const struct rc_integer_program *p,
                                struct rc_integer_program **out, struct rc_error *error)
{
    struct rc_integer_program *copy = malloc(sizeof(*copy));
    if (copy == NULL) return rc_fail(error, RC_MEMORY, 0, 0);
    *copy = *p;
    copy->insns = malloc(p->count * sizeof(*copy->insns));
    if (copy->insns == NULL) { free(copy); return rc_fail(error, RC_MEMORY, 0, 0); }
    memcpy(copy->insns, p->insns, p->count * sizeof(*copy->insns));
    *out = copy;
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_builder_finalize(const struct rc_builder *b,
                                    struct rc_integer_program **out, struct rc_error *error)
{
    if (b == NULL || out == NULL || *out != NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    enum rc_status status = validate(&b->program, error);
    return status == RC_OK ? rc_integer_copy(&b->program, out, error) : status;
}

void rc_builder_destroy(struct rc_builder *b)
{
    if (b != NULL) { free(b->program.insns); free(b); }
}

void rc_integer_program_destroy(struct rc_integer_program *p)
{
    if (p != NULL) { free(p->insns); free(p); }
}

enum rc_status rc_integer_interpret(const struct rc_integer_program *p,
                                     const uint64_t *arguments, size_t count, uint64_t budget,
                                     uint64_t *result, struct rc_error *error)
{
    if (p == NULL || result == NULL || count != p->arguments || (count != 0 && arguments == NULL))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    uint64_t regs[RC_INTEGER_MAX_REGISTERS];
    size_t pc = 0;
    bool limited = budget != 0;
    for (;;) {
        if (limited && budget-- == 0) return rc_fail(error, RC_BUDGET, pc, 0);
        struct rc_integer_insn i = p->insns[pc++];
        uint64_t a = 0, b = 0, value = 0;
        if (i.op >= RC_INT_ADD && i.op <= RC_INT_SLE) { a = regs[i.left]; b = regs[i.right]; }
        switch (i.op) {
        case RC_INT_ARGUMENT: value = arguments[i.immediate]; break;
        case RC_INT_CONSTANT: value = i.immediate; break;
        case RC_INT_ADD: value = a + b; break;
        case RC_INT_SUB: value = a - b; break;
        case RC_INT_MUL: value = a * b; break;
        case RC_INT_AND: value = a & b; break;
        case RC_INT_OR: value = a | b; break;
        case RC_INT_XOR: value = a ^ b; break;
        case RC_INT_SHL: value = a << (b & 63); break;
        case RC_INT_SHR: value = a >> (b & 63); break;
        case RC_INT_EQ: value = a == b; break;
        case RC_INT_NE: value = a != b; break;
        case RC_INT_ULT: value = a < b; break;
        case RC_INT_ULE: value = a <= b; break;
        /* Flip the sign bit to order two's-complement signed bit patterns,
         * without implementation-defined unsigned-to-signed conversions. */
        case RC_INT_SLT: value = (a ^ (UINT64_C(1) << 63)) < (b ^ (UINT64_C(1) << 63)); break;
        case RC_INT_SLE: value = (a ^ (UINT64_C(1) << 63)) <= (b ^ (UINT64_C(1) << 63)); break;
        case RC_INT_JUMP: pc = i.target; continue;
        case RC_INT_BRANCH: if (regs[i.left] != 0) pc = i.target; continue;
        case RC_INT_RETURN: *result = regs[i.left]; return rc_fail(error, RC_OK, 0, 0);
        }
        regs[i.dst] = value;
    }
}
