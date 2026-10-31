#define _GNU_SOURCE
#include "trace.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

void rc_trace_record(struct rc_trace *t, const struct rc_program *p,
                     size_t pc, const struct rc_state *s, bool returned)
{
    if (t->stop != RC_TRACE_RECORDING)
        return;
    if (t->count == t->capacity) {
        t->stop = RC_TRACE_LIMIT;
        return;
    }
    struct rc_insn insn = p->insns[pc];
    bool taken = insn.op == RC_JZ ? s->zf != 0 : insn.op == RC_JNZ && s->zf == 0;
    uint32_t exit_pc = 0;
    if (insn.op == RC_JZ || insn.op == RC_JNZ)
        exit_pc = taken ? (uint32_t)(pc + 1) : insn.immediate;
    t->ops[t->count++] = (struct rc_trace_op){insn, (uint32_t)pc,
        (uint32_t)s->pc, exit_pc, taken};
    if (returned)
        t->stop = RC_TRACE_RETURN;
    else if (s->pc == t->header)
        t->stop = RC_TRACE_LOOP;
    else if (t->count == t->capacity)
        t->stop = RC_TRACE_LIMIT;
}

enum rc_status rc_trace_validate(const struct rc_program *p,
                                  const struct rc_trace *t, struct rc_error *error)
{
    if (p == NULL || t == NULL || t->ops == NULL || t->count == 0 || t->count > t->capacity ||
        t->capacity > RC_TRACE_MAX_OPS || t->header >= p->count ||
        t->entry.pc != t->header || t->entry.zf > 1 ||
        (t->stop != RC_TRACE_LOOP && t->stop != RC_TRACE_RETURN))
        return rc_fail(error, RC_INVALID_IR, 0, 0);
    enum rc_status status = rc_validate(p, error);
    if (status != RC_OK)
        return status;
    size_t pc = t->header;
    for (size_t i = 0; i < t->count; ++i) {
        const struct rc_trace_op *op = &t->ops[i];
        if (pc >= p->count || op->pc != pc ||
            op->insn.op != p->insns[pc].op || op->insn.immediate != p->insns[pc].immediate)
            return rc_fail(error, RC_INVALID_IR, i, 0);
        struct rc_insn insn = op->insn;
        size_t next = pc + 1;
        if (insn.op == RC_JMP)
            next = insn.immediate;
        else if (insn.op == RC_JZ || insn.op == RC_JNZ) {
            next = op->taken ? insn.immediate : pc + 1;
            size_t side = op->taken ? pc + 1 : insn.immediate;
            if (side >= p->count || op->exit_pc != side)
                return rc_fail(error, RC_INVALID_IR, i, 0);
        }
        if (op->next != next || (insn.op == RC_RET && i + 1 != t->count) ||
            (i + 1 < t->count && next == t->header))
            return rc_fail(error, RC_INVALID_IR, i, 0);
        pc = next;
    }
    bool returned = t->ops[t->count - 1].insn.op == RC_RET;
    if ((t->stop == RC_TRACE_RETURN) != returned || (!returned && pc != t->header))
        return rc_fail(error, RC_INVALID_IR, t->count, 0);
    return rc_fail(error, RC_OK, 0, 0);
}

/* The emitter uses only caller-saved RAX, RCX, RDX, RSI and RDI. No calls,
 * stack accesses or callee-saved registers occur. Offsets are ABI assertions. */
_Static_assert(offsetof(struct rc_state, rax) == 0, "trace RAX offset");
_Static_assert(offsetof(struct rc_state, argument) == 8, "trace argument offset");
_Static_assert(offsetof(struct rc_state, pc) == 16, "trace PC offset");
_Static_assert(offsetof(struct rc_state, zf) == 24, "trace ZF offset");
_Static_assert(offsetof(struct rc_state, remaining) == 32, "trace fuel offset");
_Static_assert(sizeof(rc_trace_function) == sizeof(void *), "trace pointer size");

struct output { uint8_t *bytes; size_t count; };
static void byte(struct output *o, uint8_t b)
{
    if (o->bytes != NULL)
        o->bytes[o->count] = b;
    ++o->count;
}
static void word(struct output *o, uint32_t value)
{
    for (unsigned i = 0; i < 32; i += 8)
        byte(o, (uint8_t)(value >> i));
}
static void sequence(struct output *o, const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        byte(o, bytes[i]);
}
static void snapshot(struct output *o, uint32_t pc, enum rc_trace_exit reason)
{
    /* SETZ precedes MOV EDX,pc. Neither snapshot stores nor MOV change flags. */
    const uint8_t state[] = {0x48,0x89,0x07,0x0f,0x94,0x47,0x18,
                            0x48,0x89,0x4f,0x20,0xba};
    sequence(o, state, sizeof(state));
    word(o, pc);
    const uint8_t store[] = {0x48,0x89,0x57,0x10,0xb8};
    sequence(o, store, sizeof(store));
    word(o, (uint32_t)reason);
    byte(o, 0xc3);
}
static bool fits32(uint64_t value)
{
    return value <= INT32_MAX || value >= UINT64_MAX - (uint64_t)INT32_MAX;
}

static size_t emit_body(struct output *o, const struct rc_trace *t, bool optimize, size_t exits)
{
    const uint8_t entry[] = {0xf3,0x0f,0x1e,0xfa,0x48,0x8b,0x07,
        0x48,0x8b,0x77,0x08,0x48,0x8b,0x4f,0x20,0x80,0x7f,0x18,0x01};
    sequence(o, entry, sizeof(entry));
    size_t loop = o->count, guard = 0;
    for (size_t i = 0; i < t->count; ++i) {
        struct rc_insn insn = t->ops[i].insn;
        if (optimize && (insn.op == RC_ADD || insn.op == RC_SUB)) {
            size_t end = i;
            uint64_t delta = 0;
            while (end < t->count) {
                struct rc_insn next = t->ops[end].insn;
                if (next.op == RC_ADD) delta += rc_extend(next.immediate);
                else if (next.op == RC_SUB) delta -= rc_extend(next.immediate);
                else if (next.op != RC_NOP && next.op != RC_JMP) break;
                ++end;
            }
            if (fits32(delta) || fits32(UINT64_C(0) - delta)) {
                bool add = fits32(delta);
                insn = (struct rc_insn){add ? RC_ADD : RC_SUB,
                    (uint32_t)(add ? delta : UINT64_C(0) - delta)};
                i = end - 1;
            }
        }
        switch (insn.op) {
        case RC_INPUT: {
            const uint8_t input[] = {0x48,0x89,0xf0};
            sequence(o,input,sizeof(input));
            break;
        }
        case RC_CONST: byte(o,0xb8); word(o,insn.immediate); break;
        case RC_ADD:
        case RC_SUB:
        case RC_CMP:
            byte(o,0x48); byte(o,insn.op == RC_ADD ? 0x05 : insn.op == RC_SUB ? 0x2d : 0x3d);
            word(o,insn.immediate);
            break;
        case RC_TEST: byte(o,0x48); byte(o,0x85); byte(o,0xc0); break;
        case RC_JZ:
        case RC_JNZ: {
            bool expect_zero = (insn.op == RC_JZ) == t->ops[i].taken;
            byte(o,0x0f); byte(o,expect_zero ? 0x85 : 0x84);
            struct output query = {0};
            snapshot(&query,0,RC_TRACE_SIDE_EXIT);
            size_t target = exits + guard++ * query.count;
            word(o,(uint32_t)target - (uint32_t)(o->count + 4));
            break;
        }
        case RC_RET: snapshot(o,t->ops[i].next,RC_TRACE_RETURNED); break;
        case RC_JMP: break; /* Recorded successor is the next emitted operation. */
        case RC_NOP: if (!optimize) byte(o,0x90); break;
        }
    }
    if (t->stop == RC_TRACE_LOOP) {
        /* Save !ZF in DL before DEC uses flags for the quantum. TEST DL,DL
         * reconstructs exactly the architectural ZF on both outgoing edges. */
        const uint8_t fuel[] = {0x0f,0x95,0xc2,0x48,0xff,0xc9,0x74,0x07,0x84,0xd2,0xe9};
        sequence(o,fuel,sizeof(fuel));
        word(o,UINT32_C(0) - (uint32_t)(o->count + 4 - loop));
        byte(o,0x84); byte(o,0xd2);
        snapshot(o,t->header,RC_TRACE_YIELD);
    }
    size_t exit_base = o->count;
    for (size_t i = 0; i < t->count; ++i)
        if (t->ops[i].insn.op == RC_JZ || t->ops[i].insn.op == RC_JNZ)
            snapshot(o,t->ops[i].exit_pc,RC_TRACE_SIDE_EXIT);
    return exit_base;
}

enum rc_status rc_trace_emit(const struct rc_program *p, const struct rc_trace *t,
                              bool optimize, uint8_t *bytes, size_t capacity,
                              size_t *length, struct rc_error *error)
{
    if (length == NULL || (bytes == NULL && capacity != 0))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    *length = 0;
    enum rc_status status = rc_trace_validate(p,t,error);
    if (status != RC_OK)
        return status;
    struct output query = {0};
    size_t exits = emit_body(&query,t,optimize,0);
    /* At most 4096 operations with bounded encodings, including every exit. */
    if (query.count > INT32_MAX)
        return rc_fail(error, RC_CAPACITY, 0, 0);
    *length = query.count;
    if (bytes == NULL)
        return rc_fail(error, RC_OK, 0, 0);
    if (capacity < query.count)
        return rc_fail(error, RC_CAPACITY, 0, 0);
    struct output output = {bytes,0};
    (void)emit_body(&output,t,optimize,exits);
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_trace_map(const struct rc_program *p, const struct rc_trace *t,
                             bool optimize, size_t mapped, struct rc_code *code,
                             rc_trace_function *function, bool *allocated, struct rc_error *error)
{
    *allocated = false;
    *function = NULL;
    void *mapping = mmap(NULL,mapped,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
    if (mapping == MAP_FAILED)
        return rc_fail(error,RC_SYSTEM,0,errno);
    *allocated = true;
    *code = (struct rc_code){mapping,mapped,NULL};
    size_t emitted;
    enum rc_status status = rc_trace_emit(p,t,optimize,mapping,mapped,&emitted,error);
    if (status == RC_OK && mprotect(mapping,mapped,PROT_READ | PROT_EXEC) != 0)
        status = rc_fail(error,RC_SYSTEM,0,errno);
    if (status != RC_OK) {
        struct rc_error cleanup;
        if (rc_code_free(code,&cleanup) != RC_OK) {
            if (error != NULL) *error = cleanup;
            return RC_SYSTEM;
        }
        return status;
    }
    memcpy(function,&mapping,sizeof(*function));
    return rc_fail(error,RC_OK,0,0);
}
