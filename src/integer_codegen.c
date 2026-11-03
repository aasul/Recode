#define _GNU_SOURCE
#include "integer.h"
#include "emitter.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>

/* RAX/RCX are scratch. Memory entries also reserve RDX=data, R8=result,
 * R9=length and R10=fault PC. Assigned registers have one home across all
 * edges, including backedges; the remaining virtuals keep authoritative slots.
 * Batch entries save RBX=record layout, RBP=stride, R12=context, R13=input,
 * R14=output and R15=remaining, outside the allocation pool. */
_Static_assert(offsetof(struct rc_integer_batch_context,items) == 0, "native batch items");
_Static_assert(offsetof(struct rc_integer_batch_context,results) == 8, "native batch results");
_Static_assert(offsetof(struct rc_memory_view,data) == 0, "native view data");
_Static_assert(offsetof(struct rc_memory_view,length) == 8, "native view length");
_Static_assert(sizeof(struct rc_integer_batch_item) == 24, "native item stride");
_Static_assert(offsetof(struct rc_integer_batch_item,view) == 16, "native item view");
_Static_assert(offsetof(struct rc_integer_batch_context,count) == 16, "native batch count");
_Static_assert(offsetof(struct rc_integer_batch_context,completed) == 24, "native batch prefix");
_Static_assert(offsetof(struct rc_integer_batch_context,offset) == 32, "native batch fault");
_Static_assert(offsetof(struct rc_integer_batch_context,records) == 40, "native record layout");
_Static_assert(offsetof(struct rc_integer_batch_context,stride) == 48, "native record stride");
_Static_assert(offsetof(struct rc_integer_record_batch,stride) == 16, "record stride");
_Static_assert(offsetof(struct rc_integer_record_batch,argument_offsets) == 40, "record arguments");
_Static_assert(offsetof(struct rc_integer_record_batch,view_offset) == 56, "record view offset");
_Static_assert(offsetof(struct rc_integer_record_batch,view_length) == 64, "record view length");
_Static_assert(offsetof(struct rc_integer_result,value) == 0, "native result offset");
_Static_assert(offsetof(struct rc_integer_result,offset) == 8, "native fault offset");
static void allocate(const struct rc_integer_program *p, struct rc_integer_allocation *a)
{
    a->frame = 0;
    bool targets[RC_INTEGER_MAX_INSTRUCTIONS], arguments[2] = {false,false};
    memset(targets,0,p->count*sizeof(*targets));
    unsigned weights[RC_INTEGER_MAX_REGISTERS] = {0};
    for (size_t n = 0; n < p->count; ++n) {
        struct rc_integer_insn i = p->insns[n];
        if (i.op == RC_INT_JUMP || i.op == RC_INT_BRANCH) targets[i.target] = true;
    }
    uint64_t live = UINT64_MAX;
    for (size_t n = p->count; n-- > 0;) {
        struct rc_integer_insn i = p->insns[n];
        bool writes = rc_integer_writes(i.op);
        if (i.op == RC_INT_RETURN) live = 0;
        if (i.op == RC_INT_BRANCH || i.op == RC_INT_JUMP) live = UINT64_MAX;
        /* All registers are live on block exits. Only overwritten or unused
         * pure writes inside a block disappear; faulting reads always remain. */
        a->skip[n] = writes && !rc_integer_load(i.op) && !(live & (UINT64_C(1) << i.dst));
        if (!a->skip[n]) {
            if (writes) live &= ~(UINT64_C(1) << i.dst);
            live |= rc_integer_reads(i);
            if (i.op == RC_INT_ARGUMENT) arguments[i.immediate] = true;
            if (writes) ++weights[i.dst];
            if (rc_integer_reads(i) != 0) ++weights[i.left];
            if (rc_integer_binary(i.op)) ++weights[i.right];
        }
        if (targets[n]) live = UINT64_MAX;
    }
    for (size_t n = 0; n < RC_INTEGER_MAX_REGISTERS; ++n) a->registers[n] = -1;
    const int pool[] = {2,8,9,10,11,7,6};
    for (size_t slot = 0; slot < sizeof(pool)/sizeof(*pool); ++slot) {
        int reg = pool[slot];
        if ((p->memory && reg != 11 && reg != 7 && reg != 6) ||
            (reg == 7 && arguments[0]) || (reg == 6 && arguments[1])) continue;
        size_t best = 0;
        for (size_t n = 1; n < p->registers; ++n)
            if (weights[n] > weights[best]) best = n;
        if (weights[best] == 0) break;
        a->registers[best] = reg; weights[best] = 0;
    }
    for (size_t n = 0; n < p->registers; ++n)
        if (weights[n] != 0) a->frame = (uint32_t)((n * 8 + 8 + 15) / 16 * 16);
}
static void regop(struct output *o, uint8_t opcode, int dst, int src)
{
    byte(o,(uint8_t)(0x48 | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0)));
    byte(o,opcode); byte(o,(uint8_t)(0xc0 | ((src & 7) << 3) | (dst & 7)));
}
static void move(struct output *o, int dst, int src)
{
    if (dst != src) regop(o,0x89,dst,src);
}
static void stack(struct output *o, uint8_t opcode, int machine, uint32_t reg)
{
    bool compact = reg < 16;
    byte(o,(uint8_t)(0x48 | (machine >= 8 ? 4 : 0)));
    byte(o,opcode); byte(o,(uint8_t)((compact ? 0x44 : 0x84) | ((machine & 7) << 3)));
    byte(o,0x24);
    if (compact) byte(o,(uint8_t)(reg * 8)); else word(o,reg * 8);
}
static int source(struct output *o, const struct rc_integer_allocation *a, uint32_t reg, int scratch)
{
    if (a->registers[reg] >= 0) return a->registers[reg];
    stack(o,0x8b,scratch,reg); return scratch;
}
static void constant(struct output *o, int dst, uint64_t value)
{
    if (value <= UINT32_MAX) {
        if (dst >= 8) byte(o,0x41);
        byte(o,(uint8_t)(0xb8 + (dst & 7))); word(o,(uint32_t)value);
    } else {
        byte(o,(uint8_t)(0x48 | (dst >= 8 ? 1 : 0)));
        byte(o,(uint8_t)(0xb8 + (dst & 7)));
        word(o,(uint32_t)value); word(o,(uint32_t)(value >> 32));
    }
}
static void frame(struct output *o, uint8_t modrm, uint32_t size)
{
    if (size == 0) return;
    byte(o,0x48); byte(o,0x81); byte(o,modrm); word(o,size);
}
static void branch(struct output *o, size_t target)
{
    word(o,o->bytes != NULL ? (uint32_t)target - (uint32_t)(o->count+4) : 0);
}
static void operation(struct output *o, enum rc_integer_op op, int dst, int right)
{
    switch (op) {
    case RC_INT_ADD: regop(o,0x01,dst,right); break;
    case RC_INT_SUB: regop(o,0x29,dst,right); break;
    case RC_INT_AND: regop(o,0x21,dst,right); break;
    case RC_INT_OR: regop(o,0x09,dst,right); break;
    case RC_INT_XOR: regop(o,0x31,dst,right); break;
    case RC_INT_MUL:
        byte(o,(uint8_t)(0x48 | (dst >= 8 ? 4 : 0) | (right >= 8 ? 1 : 0)));
        byte(o,0x0f); byte(o,0xaf); byte(o,(uint8_t)(0xc0 | ((dst & 7) << 3) | (right & 7)));
        break;
    case RC_INT_SHL:
    case RC_INT_SHR:
        byte(o,(uint8_t)(0x48 | (dst >= 8 ? 1 : 0))); byte(o,0xd3);
        byte(o,(uint8_t)((op == RC_INT_SHL ? 0xe0 : 0xe8) | (dst & 7)));
        break;
    default: {
        const uint8_t conditions[] = {0x94,0x95,0x92,0x96,0x9c,0x9e};
        regop(o,0x39,dst,right);
        /* A REX prefix also selects SIL/DIL instead of AH..BH. */
        if (dst >= 4) byte(o,(uint8_t)(0x40 | (dst >= 8 ? 1 : 0)));
        byte(o,0x0f); byte(o,conditions[op-RC_INT_EQ]); byte(o,(uint8_t)(0xc0 | (dst & 7)));
        if (dst >= 4) byte(o,(uint8_t)(dst >= 8 ? 0x45 : 0x40));
        byte(o,0x0f); byte(o,0xb6); byte(o,(uint8_t)(0xc0 | ((dst & 7) << 3) | (dst & 7)));
        break;
    }
    }
}
/* disp8 addressing always encodes a displacement, including for R13. */
static void memory(struct output *o, uint8_t opcode, int reg, int base, uint8_t offset)
{
    byte(o,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0) | (base >= 8 ? 1 : 0)));
    byte(o,opcode); byte(o,(uint8_t)(0x40 | ((reg & 7) << 3) | (base & 7)));
    if ((base & 7) == 4) byte(o,0x24);
    byte(o,offset);
}
static void indexed(struct output *o, uint8_t opcode, int reg, int base, int index)
{
    byte(o,(uint8_t)(0x48 | (reg >= 8 ? 4 : 0) | (index >= 8 ? 2 : 0) | (base >= 8 ? 1 : 0)));
    byte(o,opcode); byte(o,(uint8_t)(0x44 | ((reg & 7) << 3)));
    byte(o,(uint8_t)(((index & 7) << 3) | (base & 7))); byte(o,0);
}
static void advance(struct output *o, int reg, uint8_t amount)
{
    byte(o,(uint8_t)(0x48 | (reg >= 8 ? 1 : 0))); byte(o,0x83);
    byte(o,(uint8_t)(0xc0 | (reg & 7))); byte(o,amount);
}
static void batch_exit(struct output *o, const struct rc_integer_allocation *a)
{
    memory(o,0x8b,1,12,16); regop(o,0x29,1,15);
    memory(o,0x89,1,12,24);
    frame(o,0xc4,a->frame);
    for (int reg = 15; reg >= 12; --reg) { byte(o,0x41); byte(o,(uint8_t)(0x58+(reg & 7))); }
    byte(o,0x5d); byte(o,0x5b); byte(o,0xc3);
}
static size_t emit_batch_entry(const struct rc_integer_program *p,
                                  const struct rc_integer_allocation *a,
                                  struct output *o, struct rc_integer_plan *batch)
{
    byte(o,0x53); byte(o,0x55);
    for (int reg = 12; reg <= 15; ++reg) { byte(o,0x41); byte(o,(uint8_t)(0x50+(reg & 7))); }
    move(o,12,7);
    memory(o,0x8b,13,12,0); memory(o,0x8b,14,12,8); memory(o,0x8b,15,12,16);
    memory(o,0x8b,3,12,40); memory(o,0x8b,5,12,48);
    frame(o,0xec,a->frame);
    regop(o,0x85,15,15); byte(o,0x0f); byte(o,0x84); branch(o,batch->batch_finish);
    size_t loop = o->count; /* The backedge is taken only with a nonzero remaining count. */
    regop(o,0x85,3,3); byte(o,0x0f); byte(o,0x85); branch(o,batch->batch_record);
    if (p->arguments != 0) {
        memory(o,0x8b,0,13,0); memory(o,0x8b,7,0,0);
        if (p->arguments > 1) memory(o,0x8b,6,0,8);
    }
    if (p->memory) {
        /* NULL view is the empty view. Do not dereference it. */
        constant(o,2,0); constant(o,9,0); memory(o,0x8b,0,13,16);
        regop(o,0x85,0,0); byte(o,0x0f); byte(o,0x84); branch(o,o->count+4+8);
        memory(o,0x8b,2,0,0); memory(o,0x8b,9,0,8);
    }
    byte(o,0xe9); branch(o,batch->batch_program);
    if (o->bytes == NULL) batch->batch_record = o->count;
    for (size_t n = 0; n < p->arguments; ++n) {
        memory(o,0x8b,0,3,(uint8_t)(40+8*n));
        indexed(o,0x8b,n == 0 ? 7 : 6,13,0);
    }
    if (p->memory) {
        memory(o,0x8b,0,3,56); indexed(o,0x8d,2,13,0);
        memory(o,0x8b,9,3,64);
    }
    if (o->bytes == NULL) batch->batch_program = o->count;
    return loop;
}

static void emit_binary(const struct rc_integer_allocation *a, struct output *o,
                          struct rc_integer_insn i, int dst)
{
    int left = source(o,a,i.left,0);
    int right = source(o,a,i.right,1);
    if (dst == right && (i.op == RC_INT_ADD || i.op == RC_INT_MUL ||
        i.op == RC_INT_AND || i.op == RC_INT_OR || i.op == RC_INT_XOR ||
        i.op == RC_INT_EQ || i.op == RC_INT_NE)) {
        int swap = left;
        left = right;
        right = swap;
    }
    /* Shifts need CL; other destructive overlaps need a saved right operand. */
    if (i.op == RC_INT_SHL || i.op == RC_INT_SHR || (dst == right && dst != left)) {
        move(o,1,right);
        right = 1;
    }
    move(o,dst,left);
    operation(o,i.op,dst,right);
}

static void emit_load(const struct rc_integer_allocation *a, struct output *o,
                        struct rc_integer_insn i, size_t pc, int dst, size_t failure)
{
    byte(o,0x41); byte(o,0xba); word(o,(uint32_t)pc);
    move(o,0,source(o,a,i.left,0)); move(o,1,9);
    /* Compare before subtracting so an offset beyond the view cannot wrap. */
    regop(o,0x39,0,1); byte(o,0x0f); byte(o,0x87); branch(o,failure);
    regop(o,0x29,1,0);
    byte(o,0x48); byte(o,0x83); byte(o,0xf9); byte(o,(uint8_t)(1u << (i.op-RC_INT_LOAD_U8)));
    byte(o,0x0f); byte(o,0x82); branch(o,failure);
    if (i.op == RC_INT_LOAD_U64) byte(o,0x48);
    if (i.op == RC_INT_LOAD_U8 || i.op == RC_INT_LOAD_U16) {
        byte(o,0x0f); byte(o,i.op == RC_INT_LOAD_U8 ? 0xb6 : 0xb7);
    } else byte(o,0x8b);
    byte(o,0x04); byte(o,0x02);
    move(o,dst,0);
}

static size_t emit_batch_finish(const struct rc_integer_program *p,
                                   const struct rc_integer_allocation *a,
                                   struct output *o, struct rc_integer_plan *batch, size_t loop)
{
    if (o->bytes == NULL) batch->batch_success = o->count;
    memory(o,0x89,0,14,0);
    regop(o,0x01,13,5); advance(o,14,8);
    advance(o,15,255); /* ADD -1, keeping flags for the loop backedge. */
    byte(o,0x0f); byte(o,0x85); branch(o,loop);
    if (o->bytes == NULL) batch->batch_finish = o->count;
    constant(o,0,RC_OK); byte(o,0xe9); branch(o,batch->batch_exit);
    if (o->bytes == NULL) batch->batch_failure = o->count;
    if (p->memory) memory(o,0x89,10,12,32);
    constant(o,0,RC_BOUNDS);
    if (o->bytes == NULL) batch->batch_exit = o->count;
    batch_exit(o,a);
    return batch->batch_failure;
}

static size_t emit_body(const struct rc_integer_program *p, const struct rc_integer_allocation *a,
                        struct output *o, size_t *offsets, size_t failure, struct rc_integer_plan *batch)
{
    byte(o,0xf3); byte(o,0x0f); byte(o,0x1e); byte(o,0xfa);
    size_t loop = 0;
    if (batch != NULL) {
        loop = emit_batch_entry(p,a,o,batch);
    } else {
        frame(o,0xec,a->frame);
        if (p->memory) move(o,9,1);
    }
    for (size_t n = 0; n < p->count; ++n) {
        if (o->bytes == NULL) offsets[n] = o->count;
        if (a->skip[n]) continue;
        struct rc_integer_insn i = p->insns[n];
        int dst = a->registers[i.dst] >= 0 ? a->registers[i.dst] : 0;
        if (rc_integer_binary(i.op)) {
            emit_binary(a,o,i,dst);
        } else switch (i.op) {
        case RC_INT_ARGUMENT: move(o,dst,i.immediate == 0 ? 7 : 6); break;
        case RC_INT_CONSTANT: constant(o,dst,i.immediate); break;
        case RC_INT_LOAD_U8:
        case RC_INT_LOAD_U16:
        case RC_INT_LOAD_U32:
        case RC_INT_LOAD_U64:
            emit_load(a,o,i,n,dst,failure);
            break;
        case RC_INT_BRANCH: {
            int condition = source(o,a,i.left,0);
            regop(o,0x85,condition,condition);
            byte(o,0x0f); byte(o,0x85); branch(o,o->bytes != NULL ? offsets[i.target] : 0); continue;
        }
        case RC_INT_JUMP: byte(o,0xe9); branch(o,o->bytes != NULL ? offsets[i.target] : 0); continue;
        case RC_INT_RETURN:
            move(o,0,source(o,a,i.left,0));
            if (batch != NULL) {
                if (n+1 != p->count) { byte(o,0xe9); branch(o,batch->batch_success); }
                continue;
            }
            if (p->memory) {
                byte(o,0x49); byte(o,0x89); byte(o,0x00); byte(o,0x31); byte(o,0xc0);
            }
            frame(o,0xc4,a->frame); byte(o,0xc3); continue;
        default: break; /* Binary operations were emitted above. */
        }
        if (a->registers[i.dst] < 0) stack(o,0x89,0,i.dst);
    }
    if (batch != NULL)
        return emit_batch_finish(p,a,o,batch,loop);
    size_t failure_offset = o->count;
    if (p->memory) {
        byte(o,0x4d); byte(o,0x89); byte(o,0x50); byte(o,0x08);
        byte(o,0xb8); word(o,RC_BOUNDS); frame(o,0xc4,a->frame); byte(o,0xc3);
    }
    return failure_offset;
}

enum rc_status rc_integer_prepare(const struct rc_integer_program *p,
                                   struct rc_integer_plan *plan, struct rc_error *error)
{
    if (plan == NULL) return rc_fail(error,RC_ARGUMENT,0,0);
    enum rc_status status = rc_integer_validate(p,error);
    if (status != RC_OK) return status;
    plan->program = p;
    plan->batch = 0;
    allocate(p,&plan->allocation);
    struct output query = {0};
    plan->failure = emit_body(p,&plan->allocation,&query,plan->offsets,0,NULL);
    plan->length = query.count;
    return rc_fail(error,RC_OK,0,0);
}

enum rc_status rc_integer_prepare_batch(const struct rc_integer_program *p,
                                         struct rc_integer_plan *plan, struct rc_error *error)
{
    enum rc_status status = rc_integer_prepare(p,plan,error);
    if (status != RC_OK) return status;
    plan->batch_success = plan->batch_failure = plan->batch_finish = plan->batch_exit = 0;
    plan->batch_record = plan->batch_program = 0;
    plan->batch = (plan->length+15)/16*16;
    struct output query = {.count = plan->batch};
    (void)emit_body(p,&plan->allocation,&query,plan->batch_offsets,0,plan);
    plan->length = query.count;
    return RC_OK;
}

enum rc_status rc_integer_emit(const struct rc_integer_program *p, uint8_t *bytes,
                                size_t capacity, size_t *length, struct rc_error *error)
{
    if (p == NULL || length == NULL || (bytes == NULL && capacity != 0))
        return rc_fail(error,RC_ARGUMENT,0,0);
    struct rc_integer_plan plan;
    enum rc_status status = rc_integer_prepare(p,&plan,error);
    if (status != RC_OK) return status;
    *length = plan.length;
    if (bytes == NULL) return rc_fail(error,RC_OK,0,0);
    if (capacity < plan.length) return rc_fail(error,RC_CAPACITY,0,0);
    struct output output = {bytes,0};
    (void)emit_body(p,&plan.allocation,&output,plan.offsets,plan.failure,NULL);
    return rc_fail(error,RC_OK,0,0);
}

void rc_integer_write_prepared(struct rc_integer_plan *plan, uint8_t *bytes)
{
    struct output output = {bytes,0};
    (void)emit_body(plan->program,&plan->allocation,&output,plan->offsets,plan->failure,NULL);
    if (plan->batch != 0) {
        while (output.count < plan->batch) byte(&output,0x90);
        (void)emit_body(plan->program,&plan->allocation,&output,plan->batch_offsets,plan->batch_failure,plan);
    }
}

enum rc_status rc_integer_map_prepared(struct rc_integer_plan *plan, size_t mapped,
                               struct rc_code *code, rc_integer_function *function,
                               bool *allocated, struct rc_error *error)
{
    *allocated = false;
    *function = NULL;
    if (mapped < plan->length) return rc_fail(error,RC_CAPACITY,0,0);
    void *mapping = mmap(NULL, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return rc_fail(error, RC_SYSTEM, 0, errno);
    *allocated = true;
    *code = (struct rc_code){mapping, mapped, NULL};
    rc_integer_write_prepared(plan,mapping);
    if (mprotect(mapping, mapped, PROT_READ | PROT_EXEC) != 0) {
        enum rc_status status = rc_fail(error,RC_SYSTEM,0,errno);
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

enum rc_status rc_integer_map(const struct rc_integer_program *p, size_t mapped,
                               struct rc_code *code, rc_integer_function *function,
                               bool *allocated, struct rc_error *error)
{
    *allocated = false;
    *function = NULL;
    struct rc_integer_plan plan;
    enum rc_status status = rc_integer_prepare(p,&plan,error);
    return status == RC_OK ? rc_integer_map_prepared(&plan,mapped,code,function,allocated,error) : status;
}
