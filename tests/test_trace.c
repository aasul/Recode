#define _GNU_SOURCE
#include "trace.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)

static bool stop_recording(struct rc_observer *o, struct rc_state *s)
{
    (void)s;
    return o->context != NULL;
}
static void record_after(struct rc_observer *o, size_t pc, struct rc_state *s, bool returned)
{
    (void)pc; (void)s; (void)returned;
    if (o->recording != NULL && o->recording->stop != RC_TRACE_RECORDING) {
        o->recording = NULL;
        o->context = o;
    }
}
static void record(const struct rc_program *p, struct rc_trace *t, struct rc_state state)
{
    t->entry = state;
    t->header = (uint32_t)state.pc;
    struct rc_observer o = {NULL,t,stop_recording,record_after};
    rc_interpret_observed(p,&state,&o);
}
static bool resume_before(struct rc_observer *o, struct rc_state *s)
{
    (void)o; (void)s; return false;
}
static void resume_after(struct rc_observer *o, size_t pc, struct rc_state *s, bool returned)
{
    (void)o; (void)pc; (void)s; (void)returned;
}
static void resume(const struct rc_program *p, struct rc_state *s)
{
    struct rc_observer o = {NULL,NULL,resume_before,resume_after};
    rc_interpret_observed(p,s,&o);
}
static rc_trace_function compile(const struct rc_program *p, struct rc_trace *t,
                                 bool optimize, struct rc_code *code)
{
    size_t size;
    CHECK(rc_trace_emit(p,t,optimize,NULL,0,&size,NULL) == RC_OK);
    bool allocated;
    rc_trace_function fn;
    CHECK(rc_trace_map(p,t,optimize,size,code,&fn,&allocated,NULL) == RC_OK && allocated);
    CHECK(fn != NULL && code->function == NULL);
    return fn;
}

static void test_snapshots(void)
{
    struct rc_insn insns[] = {{RC_INPUT,0},{RC_TEST,0},{RC_JZ,8},
        {RC_CMP,5},{RC_JZ,10},{RC_SUB,1},{RC_JNZ,1},{RC_RET,0},
        {RC_CONST,9},{RC_RET,0},{RC_INPUT,0},{RC_RET,0}};
    struct rc_program p = {insns,12};
    struct rc_trace_op ops[32];
    struct rc_trace t = {.ops = ops,.capacity = 32};
    record(&p,&t,(struct rc_state){.rax = 7,.argument = 77,.pc = 1});
    CHECK(t.stop == RC_TRACE_LOOP && t.count == 6);
    const uint32_t order[] = {1,2,3,4,5,6};
    for (size_t i = 0; i < 6; ++i) CHECK(t.ops[i].pc == order[i]);
    CHECK(!ops[1].taken && !ops[3].taken && ops[5].taken);
    CHECK(ops[1].exit_pc == 8 && ops[3].exit_pc == 10 && ops[5].exit_pc == 7);
    CHECK(t.entry.rax == 7 && t.entry.argument == 77 && t.entry.pc == 1);
    CHECK(rc_trace_validate(&p,&t,NULL) == RC_OK);
    CHECK(rc_trace_validate(NULL,&t,NULL) == RC_INVALID_IR);
    uint8_t too_small[8]; size_t needed;
    memset(too_small,0xaa,sizeof(too_small));
    CHECK(rc_trace_emit(&p,&t,true,too_small,sizeof(too_small),&needed,NULL) == RC_CAPACITY);
    for (size_t i = 0; i < sizeof(too_small); ++i) CHECK(too_small[i] == 0xaa);
    for (unsigned opt = 0; opt < 2; ++opt) {
        struct rc_code code = {0};
        rc_trace_function fn = compile(&p,&t,opt != 0,&code);
        const uint64_t inputs[] = {0,5,1,6,7};
        const size_t pcs[] = {8,10,7,10,10};
        const uint64_t values[] = {0,5,0,5,5};
        for (size_t i = 0; i < 5; ++i) {
            struct rc_state s = {.rax = inputs[i],.argument = 77,.pc = 1,.remaining = 100};
            CHECK(fn(&s) == RC_TRACE_SIDE_EXIT);
            CHECK(s.pc == pcs[i] && s.rax == values[i] && s.zf == 1 && s.argument == 77);
            CHECK(s.remaining == 100 - (i == 3 ? 1U : i == 4 ? 2U : 0U));
            resume(&p,&s);
            CHECK(s.rax == (i == 0 ? 9U : i == 2 ? 0U : 77U));
        }
        struct rc_state yield = {.rax = 9,.argument = UINT64_MAX,.pc = 1,.zf = 1,.remaining = 1};
        CHECK(fn(&yield) == RC_TRACE_YIELD);
        CHECK(yield.pc == 1 && yield.rax == 8 && yield.zf == 0 && yield.remaining == 0);
        CHECK(rc_code_free(&code,NULL) == RC_OK);
    }
    ops[0].next = 7;
    CHECK(rc_trace_validate(&p,&t,NULL) == RC_INVALID_IR);
    ops[0].next = 2;
    ops[1].exit_pc = 99;
    CHECK(rc_trace_validate(&p,&t,NULL) == RC_INVALID_IR);
    ops[1].exit_pc = 8;
    t.entry.pc = 0;
    CHECK(rc_trace_validate(&p,&t,NULL) == RC_INVALID_IR);
    t.entry.pc = 1; t.entry.zf = 2;
    CHECK(rc_trace_validate(&p,&t,NULL) == RC_INVALID_IR);
}

static void test_flags_and_optimization(void)
{
    /* A zero-sum arithmetic run must still set ZF, and INPUT preserves it. */
    struct rc_insn insns[] = {{RC_INPUT,0},{RC_ADD,1},{RC_NOP,0},{RC_JMP,4},
        {RC_SUB,1},{RC_INPUT,0},{RC_JZ,8},{RC_JMP,1},{RC_RET,0}};
    struct rc_program p = {insns,9};
    struct rc_trace_op ops[32];
    struct rc_trace t = {.ops = ops,.capacity = 32};
    record(&p,&t,(struct rc_state){.rax = UINT64_MAX,.argument = UINT64_MAX,.pc = 1,.zf = 1});
    CHECK(t.stop == RC_TRACE_LOOP && t.count == 7);
    size_t sizes[2];
    for (unsigned opt = 0; opt < 2; ++opt) {
        struct rc_code code = {0};
        rc_trace_function fn = compile(&p,&t,opt != 0,&code);
        sizes[opt] = code.size;
        for (uint64_t zf = 0; zf < 2; ++zf) {
            struct rc_state s = {.rax = 0,.argument = 123,.pc = 1,.zf = (uint8_t)zf,.remaining = 4};
            CHECK(fn(&s) == RC_TRACE_SIDE_EXIT);
            CHECK(s.rax == 123 && s.argument == 123 && s.pc == 8 && s.zf == 1);
            s = (struct rc_state){.rax = UINT64_MAX,.argument = UINT64_MAX,.pc = 1,.remaining = 1};
            CHECK(fn(&s) == RC_TRACE_YIELD && s.rax == UINT64_MAX && s.zf == 0 && s.pc == 1);
        }
        CHECK(rc_code_free(&code,NULL) == RC_OK);
    }
    CHECK(sizes[1] < sizes[0]);
    /* The first guard consumes entry ZF; restoring it must not compare RAX. */
    struct rc_insn entry[] = {{RC_INPUT,0},{RC_TEST,0},{RC_JZ,5},
        {RC_CONST,42},{RC_JMP,2},{RC_RET,0}};
    p = (struct rc_program){entry,6}; t = (struct rc_trace){.ops = ops,.capacity = 32};
    record(&p,&t,(struct rc_state){.rax = 99,.pc = 2,.zf = 0});
    struct rc_code code = {0};
    rc_trace_function fn = compile(&p,&t,true,&code);
    struct rc_state s = {.rax = 99,.argument = 1,.pc = 2,.zf = 1,.remaining = 1};
    CHECK(fn(&s) == RC_TRACE_SIDE_EXIT && s.pc == 5 && s.rax == 99 && s.zf == 1);
    CHECK(rc_code_free(&code,NULL) == RC_OK);
    /* A recorded return exits after RET and propagates RAX and ZF. */
    t = (struct rc_trace){.ops = ops,.capacity = 32};
    record(&p,&t,(struct rc_state){.rax = 99,.pc = 2,.zf = 1});
    CHECK(t.stop == RC_TRACE_RETURN);
    fn = compile(&p,&t,true,&code);
    s = (struct rc_state){.rax = 71,.argument = 123,.pc = 2,.zf = 1,.remaining = 1};
    CHECK(fn(&s) == RC_TRACE_RETURNED && s.pc == 6 && s.rax == 71 && s.zf == 1);
    CHECK(rc_code_free(&code,NULL) == RC_OK);
}

/* A bounded independent trace walker checks every boundary, including flags.
 * It follows original IR and stops at the first different recorded decision. */
static enum rc_trace_exit reference(const struct rc_program *p, const struct rc_trace *t,
                                     struct rc_state *s)
{
    for (;;) {
        for (size_t i = 0; i < t->count; ++i) {
            struct rc_insn insn = p->insns[s->pc++];
            uint64_t immediate = insn.immediate <= INT32_MAX ? insn.immediate
                : UINT64_MAX - (UINT32_MAX - (uint64_t)insn.immediate);
            bool taken = false;
            switch (insn.op) {
            case RC_INPUT: s->rax = s->argument; break;
            case RC_CONST: s->rax = insn.immediate; break;
            case RC_ADD: s->rax += immediate; s->zf = s->rax == 0; break;
            case RC_SUB: s->rax -= immediate; s->zf = s->rax == 0; break;
            case RC_CMP: s->zf = s->rax == immediate; break;
            case RC_TEST: s->zf = s->rax == 0; break;
            case RC_JZ: taken = s->zf != 0; if (taken) s->pc = insn.immediate; break;
            case RC_JNZ: taken = s->zf == 0; if (taken) s->pc = insn.immediate; break;
            case RC_JMP: s->pc = insn.immediate; break;
            case RC_NOP: break;
            case RC_RET: return RC_TRACE_RETURNED;
            }
            if ((insn.op == RC_JZ || insn.op == RC_JNZ) && taken != t->ops[i].taken)
                return RC_TRACE_SIDE_EXIT;
        }
        if (--s->remaining == 0) return RC_TRACE_YIELD;
    }
}
static uint64_t random_state = UINT64_C(0x875b3e02ab117dad);
static uint64_t random_value(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}
static void test_random_snapshots(void)
{
    for (unsigned trial = 0; trial < 300; ++trial) {
        uint32_t a = (uint32_t)random_value(), b = (uint32_t)random_value();
        struct rc_insn insns[] = {{RC_INPUT,0},{RC_TEST,0},{RC_ADD,a},{RC_SUB,b},
            {RC_NOP,0},{RC_JMP,6},{RC_SUB,a},{RC_ADD,b},{RC_CMP,0},
            {RC_JZ,13},{RC_INPUT,0},{RC_TEST,0},{RC_JNZ,2},{RC_RET,0}};
        struct rc_program p = {insns,14};
        struct rc_trace_op ops[32];
        struct rc_trace t = {.ops = ops,.capacity = 32};
        record(&p,&t,(struct rc_state){.rax = 3,.argument = 3,.pc = 2,.zf = 1});
        CHECK(t.stop == RC_TRACE_LOOP);
        for (unsigned opt = 0; opt < 2; ++opt) {
            struct rc_code code = {0};
            rc_trace_function fn = compile(&p,&t,opt != 0,&code);
            for (unsigned sample = 0; sample < 16; ++sample) {
                struct rc_state actual = {.rax = sample < 2 ? sample : random_value(),
                    .argument = sample < 4 ? sample : random_value(),.pc = 2,
                    .zf = (uint8_t)(sample % 2),.remaining = 1 + random_value() % 4};
                struct rc_state expected = actual;
                enum rc_trace_exit reason = reference(&p,&t,&expected);
                CHECK(fn(&actual) == reason);
                CHECK(actual.rax == expected.rax && actual.zf == expected.zf && actual.pc == expected.pc);
                CHECK(actual.argument == expected.argument && actual.remaining == expected.remaining);
            }
            CHECK(rc_code_free(&code,NULL) == RC_OK);
        }
    }
}

static const uint8_t countdown[] = {0x48,0x89,0xf8,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
static struct rc_runtime *create(size_t budget)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = RC_POLICY_TRACE; c.trace_hot_threshold = 2; c.retry_calls = 2;
    c.executable_budget = budget;
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    return r;
}
static rc_handle reg(struct rc_runtime *r, const uint8_t *bytes, size_t length)
{
    rc_handle h; CHECK(rc_runtime_register(r,bytes,length,&h,NULL) == RC_OK); return h;
}
static void call(struct rc_runtime *r, rc_handle h, uint64_t input)
{
    uint64_t result = 99;
    CHECK(rc_runtime_call(r,h,input,&result,NULL) == RC_OK && result == 0);
}
static struct rc_function_stats stats(struct rc_runtime *r, rc_handle h)
{
    struct rc_function_stats s; CHECK(rc_runtime_function_stats(r,h,&s,NULL) == RC_OK); return s;
}
static void test_runtime(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = create(page);
    rc_handle h = reg(r,countdown,sizeof(countdown));
    call(r,h,2);
    CHECK(stats(r,h).trace_recordings == 0);
    call(r,h,20);
    struct rc_function_stats s = stats(r,h);
    CHECK(s.trace_recordings == 1 && s.trace_recorded_ops == 2 && s.compilations == 1);
    CHECK(s.trace_entries == 1 && s.trace_side_exits == 1 && s.trace_iterations > 0);
    call(r,h,1); call(r,h,10000);
    CHECK(stats(r,h).compilations == 1 && stats(r,h).trace_yields > 0);
    rc_handle alias = reg(r,countdown,sizeof(countdown));
    call(r,alias,42);
    CHECK(stats(r,alias).compilations == 0 && stats(r,alias).trace_entries == 1);
    uint8_t other[sizeof(countdown) + 1];
    memcpy(other,countdown,sizeof(countdown)-1); other[sizeof(countdown)-1] = 0x90;
    other[sizeof(countdown)] = 0xc3;
    rc_handle b = reg(r,other,sizeof(other));
    call(r,b,100);
    struct rc_runtime_stats all;
    CHECK(rc_runtime_get_stats(r,&all,NULL) == RC_OK);
    CHECK(all.evictions == 1 && all.executable_bytes == page && all.peak_executable_bytes == page);
    CHECK(rc_runtime_invalidate(r,b,NULL) == RC_OK);
    for (unsigned i = 0; i < 3; ++i) call(r,h,100);
    CHECK(stats(r,h).compilations == 2);
    CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    call(r,alias,55);
    CHECK(rc_runtime_clear(r,NULL) == RC_OK);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    r = create(page-1); h = reg(r,countdown,sizeof(countdown));
    for (unsigned i = 0; i < 50; ++i) call(r,h,20);
    s = stats(r,h);
    CHECK(s.trace_entries == 0 && s.compilation_attempts == 3 && s.compilation_failures == 3);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = RC_POLICY_TRACE; c.trace_max_instructions = 1; c.trace_hot_threshold = 1;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK); h = reg(r,countdown,sizeof(countdown));
    for (unsigned i = 0; i < 20; ++i) call(r,h,10);
    CHECK(stats(r,h).trace_aborts == 1 && stats(r,h).compilation_attempts == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    c.trace_max_instructions = 256; c.trace_record_only = true;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK); h = reg(r,countdown,sizeof(countdown));
    for (unsigned i = 0; i < 20; ++i) call(r,h,100);
    CHECK(stats(r,h).trace_recordings == 1 && stats(r,h).compilation_attempts == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    r = create(page); h = reg(r,countdown,sizeof(countdown)); call(r,h,100);
    for (unsigned i = 0; i < 8; ++i) call(r,h,1);
    CHECK(stats(r,h).trace_retirements == 1);
    uint64_t entries = stats(r,h).trace_entries;
    call(r,h,100);
    CHECK(stats(r,h).trace_entries == entries);
    call(r,h,100); call(r,h,100);
    CHECK(stats(r,h).trace_entries > entries && stats(r,h).compilations == 1);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    c.trace_max_instructions = RC_TRACE_MAX_OPS + 1;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_ARGUMENT);
}

static void test_candidate_limit(void)
{
    const uint8_t two[] = {0x48,0x89,0xf8,0x48,0x83,0xe8,1,0x75,0xfa,
        0x48,0x89,0xf8,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
    for (size_t candidates = 1; candidates <= 2; ++candidates) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = RC_POLICY_TRACE; c.trace_hot_threshold = 2;
        c.trace_max_candidates = candidates;
        c.executable_budget = 2 * (size_t)sysconf(_SC_PAGESIZE);
        struct rc_runtime *r = NULL;
        CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
        rc_handle h = reg(r,two,sizeof(two));
        call(r,h,100);
        struct rc_runtime_stats first, after;
        CHECK(rc_runtime_get_stats(r,&first,NULL) == RC_OK);
        CHECK(stats(r,h).compilations == candidates && stats(r,h).trace_recordings == candidates);
        CHECK(first.executable_bytes == candidates * (size_t)sysconf(_SC_PAGESIZE));
        for (unsigned i = 0; i < 100; ++i) call(r,h,5+i);
        CHECK(rc_runtime_get_stats(r,&after,NULL) == RC_OK);
        CHECK(first.metadata_bytes == after.metadata_bytes && stats(r,h).compilations == candidates);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    }
}

struct worker { struct rc_runtime *runtime; rc_handle handle; atomic_bool *start; };
static void *work(void *context)
{
    struct worker *w = context;
    while (!atomic_load(w->start)) { }
    for (unsigned i = 0; i < 2000; ++i) call(w->runtime,w->handle,10 + i % 80);
    return NULL;
}
static void test_concurrency(void)
{
    struct rc_runtime *r = create((size_t)sysconf(_SC_PAGESIZE));
    rc_handle h = reg(r,countdown,sizeof(countdown));
    atomic_bool start = false;
    struct worker w = {r,h,&start};
    pthread_t threads[6];
    for (unsigned i = 0; i < 6; ++i) CHECK(pthread_create(&threads[i],NULL,work,&w) == 0);
    atomic_store(&start,true);
    for (unsigned i = 0; i < 500; ++i) {
        enum rc_status s = rc_runtime_clear(r,NULL);
        CHECK(s == RC_OK || s == RC_BUSY);
        s = rc_runtime_invalidate(r,h,NULL);
        CHECK(s == RC_OK || s == RC_BUSY);
    }
    for (unsigned i = 0; i < 6; ++i) CHECK(pthread_join(threads[i],NULL) == 0);
    CHECK(stats(r,h).invocations == 12000);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}
int main(void)
{
    alarm(60);
    test_random_snapshots(); test_snapshots(); test_flags_and_optimization(); test_runtime(); test_candidate_limit(); test_concurrency();
    puts("trace paths, guards, snapshots, native loops, fallback, reuse, eviction and concurrency passed");
    return 0;
}
