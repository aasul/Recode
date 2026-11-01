#define _POSIX_C_SOURCE 200809L
#include "integer.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
uint64_t rc_test_integer_abi(rc_integer_function function, uint64_t a, uint64_t b, uint64_t *ok);

static struct rc_integer_program *build(const struct rc_integer_insn *insns, size_t count,
                                         size_t registers, size_t arguments)
{
    struct rc_builder *b = NULL;
    struct rc_integer_program *p = NULL;
    CHECK(rc_builder_create(registers,arguments,&b,NULL) == RC_OK);
    for (size_t n = 0; n < count; ++n) CHECK(rc_builder_append(b,insns[n],NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_ARGUMENT);
    rc_builder_destroy(b);
    return p;
}

static uint64_t expected(enum rc_integer_op op, uint64_t a, uint64_t b)
{
    int64_t sa, sb;
    memcpy(&sa,&a,8); memcpy(&sb,&b,8);
    switch (op) {
    case RC_INT_ADD: return a + b;
    case RC_INT_SUB: return a - b;
    case RC_INT_MUL: return a * b;
    case RC_INT_AND: return a & b;
    case RC_INT_OR: return a | b;
    case RC_INT_XOR: return a ^ b;
    case RC_INT_SHL: return a << (b % 64);
    case RC_INT_SHR: return a >> (b % 64);
    case RC_INT_EQ: return a == b;
    case RC_INT_NE: return a != b;
    case RC_INT_ULT: return a < b;
    case RC_INT_ULE: return a <= b;
    case RC_INT_SLT: return sa < sb;
    case RC_INT_SLE: return sa <= sb;
    default: CHECK(false); return 0;
    }
}
static struct rc_runtime *runtime(enum rc_policy policy, size_t budget)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = policy; c.hot_threshold = 2; c.retry_calls = 2;
    c.initial_compile_ns = 1; c.initial_native_ns = 1;
    c.executable_budget = budget;
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    return r;
}
static void verify(const struct rc_integer_program *p, const uint64_t *args, uint64_t value,
                     rc_integer_function native)
{
    uint64_t result = 19, abi = 0;
    CHECK(rc_integer_interpret(p,args,p->arguments,10000,&result,NULL) == RC_OK && result == value);
    CHECK(rc_test_integer_abi(native,args[0],args[1],&abi) == value && abi == 1);
}
static void test_operations(void)
{
    const uint64_t values[] = {0,1,2,63,64,65,UINT32_MAX,UINT64_MAX,UINT64_MAX-1,
        UINT64_C(0x8000000000000000),INT64_MAX};
    for (enum rc_integer_op op = RC_INT_ADD; op <= RC_INT_SLE; ++op) {
        struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT,.dst=63},
            {.op=RC_INT_ARGUMENT,.dst=62,.immediate=1},
            {.op=op,.dst=63,.left=63,.right=62},{.op=RC_INT_RETURN,.left=63}};
        struct rc_integer_program *p = build(insns,4,64,2);
        size_t length; CHECK(rc_integer_emit(p,NULL,0,&length,NULL) == RC_OK);
        uint8_t bytes[256]; memset(bytes,0xaa,sizeof(bytes));
        CHECK(rc_integer_emit(p,bytes,1,&length,NULL) == RC_CAPACITY && bytes[0] == 0xaa);
        CHECK(rc_integer_emit(p,bytes,length,&length,NULL) == RC_OK && bytes[length] == 0xaa);
        struct rc_code code = {0}; rc_integer_function fn; bool allocated;
        CHECK(rc_integer_map(p,length,&code,&fn,&allocated,NULL) == RC_OK && allocated);
        for (size_t a = 0; a < sizeof(values)/sizeof(values[0]); ++a)
            for (size_t b = 0; b < sizeof(values)/sizeof(values[0]); ++b) {
                uint64_t args[] = {values[a],values[b]};
                verify(p,args,expected(op,args[0],args[1]),fn);
            }
        CHECK(rc_code_free(&code,NULL) == RC_OK);
        for (enum rc_policy policy = RC_POLICY_INTERPRET; policy <= RC_POLICY_TRACE; ++policy) {
            struct rc_runtime *r = runtime(policy,4096);
            rc_handle h = 91;
            enum rc_status status = rc_runtime_register_integer(r,p,&h,NULL);
            if (policy == RC_POLICY_TRACE) CHECK(status == RC_UNSUPPORTED && h == 91);
            else {
                CHECK(status == RC_OK);
                uint64_t args[] = {UINT64_MAX,65}, result;
                for (unsigned n = 0; n < 64; ++n) {
                    CHECK(rc_runtime_call_integer(r,h,args,2,&result,NULL) == RC_OK);
                    CHECK(result == expected(op,args[0],args[1]));
                }
                struct rc_function_stats s;
                CHECK(rc_runtime_function_stats(r,h,&s,NULL) == RC_OK);
                if (policy == RC_POLICY_FIXED) CHECK(s.jitted == 63 && s.compilations == 1);
                if (policy == RC_POLICY_INTERPRET) CHECK(s.jitted == 0 && s.compilation_attempts == 0);
                CHECK(rc_runtime_call(r,h,1,&result,NULL) == RC_ARGUMENT);
                result = 19;
                CHECK(rc_runtime_call_integer(r,h,args,1,&result,NULL) == RC_ARGUMENT && result == 19);
                CHECK(rc_runtime_call_integer(r,h,NULL,2,&result,NULL) == RC_ARGUMENT);
                CHECK(rc_runtime_call_integer(r,h,args,2,NULL,NULL) == RC_ARGUMENT);
            }
            CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
        }
        rc_integer_program_destroy(p);
    }
}
static void invalid(const struct rc_integer_insn *insns, size_t count,
                      enum rc_status status, size_t offset)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL; struct rc_error error;
    CHECK(rc_builder_create(4,2,&b,NULL) == RC_OK);
    for (size_t n = 0; n < count; ++n) CHECK(rc_builder_append(b,insns[n],NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,&error) == status && p == NULL && error.offset == offset);
    rc_builder_destroy(b);
}
static void test_compact_layout(void)
{
    const struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_BRANCH,.target=4}, {.op=RC_INT_CONSTANT,.immediate=7},
        {.op=RC_INT_RETURN}, {.op=RC_INT_CONSTANT,.immediate=9}, {.op=RC_INT_RETURN}};
    struct rc_integer_program *p = build(insns,6,1,1);
    uint8_t bytes[256]; size_t length;
    CHECK(rc_integer_emit(p,bytes,sizeof(bytes),&length,NULL) == RC_OK);
    CHECK(length == 104); /* 11-byte entry, exact operation sizes, no padding. */
    CHECK(bytes[30] == 0x0f && bytes[31] == 0x85);
    CHECK(bytes[32] == 34 && bytes[33] == 0 && bytes[34] == 0 && bytes[35] == 0);
    struct rc_code code = {0}; rc_integer_function fn; bool allocated;
    CHECK(rc_integer_map(p,length,&code,&fn,&allocated,NULL) == RC_OK);
    CHECK(fn(0,0) == 7 && fn(1,0) == 9);
    CHECK(rc_code_free(&code,NULL) == RC_OK); rc_integer_program_destroy(p);
}
static void test_validation(void)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    CHECK(rc_builder_create(0,2,&b,NULL) == RC_ARGUMENT);
    CHECK(rc_builder_create(65,2,&b,NULL) == RC_ARGUMENT);
    CHECK(rc_builder_create(1,3,&b,NULL) == RC_ARGUMENT);
    CHECK(rc_builder_create(1,2,NULL,NULL) == RC_ARGUMENT);
    CHECK(rc_builder_create(4,2,&b,NULL) == RC_OK);
    CHECK(rc_builder_create(4,2,&b,NULL) == RC_ARGUMENT);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_MISSING_RET);
    const struct rc_integer_insn bad[] = {{.op=(enum rc_integer_op)99},
        {.op=RC_INT_CONSTANT,.dst=4},{.op=RC_INT_ADD,.left=4},
        {.op=RC_INT_ADD,.right=UINT32_MAX},{.op=RC_INT_ARGUMENT,.immediate=2},
        {.op=RC_INT_JUMP,.target=4096},{.op=RC_INT_RETURN,.dst=1},
        {.op=RC_INT_CONSTANT,.left=1},{.op=RC_INT_JUMP,.right=1},
        {.op=RC_INT_ADD,.immediate=1},{.op=RC_INT_CONSTANT,.target=1}};
    for (size_t n = 0; n < sizeof(bad)/sizeof(bad[0]); ++n)
        CHECK(rc_builder_append(b,bad[n],NULL) == RC_INVALID_IR);
    rc_builder_destroy(b);
    invalid(NULL,0,RC_MISSING_RET,0);
    struct rc_integer_insn target[] = {{.op=RC_INT_JUMP,.target=2}};
    invalid(target,1,RC_INVALID_IR,0);
    struct rc_integer_insn undef[] = {{.op=RC_INT_RETURN,.left=3}};
    invalid(undef,1,RC_UNINITIALIZED,0);
    struct rc_integer_insn fall[] = {{.op=RC_INT_CONSTANT}};
    invalid(fall,1,RC_MISSING_RET,0);
    struct rc_integer_insn join[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_BRANCH,.target=3},{.op=RC_INT_CONSTANT,.dst=1},
        {.op=RC_INT_RETURN,.left=1}};
    invalid(join,4,RC_UNINITIALIZED,3);
    struct rc_integer_insn back[] = {{.op=RC_INT_JUMP,.target=2},
        {.op=RC_INT_CONSTANT,.dst=1},{.op=RC_INT_ADD,.dst=1,.left=1,.right=1},
        {.op=RC_INT_JUMP,.target=1}};
    invalid(back,4,RC_UNINITIALIZED,2);
    struct rc_integer_insn trailing[] = {{.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},
        {.op=RC_INT_RETURN}};
    invalid(trailing,3,RC_TRAILING,2);
    b = NULL; CHECK(rc_builder_create(1,0,&b,NULL) == RC_OK);
    for (unsigned n = 0; n < RC_INTEGER_MAX_INSTRUCTIONS; ++n)
        CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_JUMP},NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_JUMP},NULL) == RC_CAPACITY);
    rc_builder_destroy(b); rc_builder_destroy(NULL); rc_integer_program_destroy(NULL);
    CHECK(rc_builder_append(NULL,(struct rc_integer_insn){0},NULL) == RC_ARGUMENT);
    CHECK(rc_builder_finalize(NULL,&p,NULL) == RC_ARGUMENT);
    CHECK(rc_integer_interpret(NULL,NULL,0,0,&(uint64_t){0},NULL) == RC_ARGUMENT);
}
static void test_emitter_validation(void)
{
    struct rc_integer_insn insns[] = {{.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN}};
    struct rc_integer_program p = {1,0,2,false,insns};
    uint8_t output[128]; size_t length = 91;
    memset(output,0xa5,sizeof(output));
    insns[0].op = (enum rc_integer_op)255;
    CHECK(rc_integer_emit(&p,output,sizeof(output),&length,NULL) == RC_INVALID_IR);
    CHECK(length == 91 && output[0] == 0xa5);
    insns[0].op = RC_INT_CONSTANT;
    p.count = RC_INTEGER_MAX_INSTRUCTIONS+1;
    CHECK(rc_integer_emit(&p,output,sizeof(output),&length,NULL) == RC_INVALID_IR);
    p.count = 2; p.registers = 65;
    CHECK(rc_integer_emit(&p,output,sizeof(output),&length,NULL) == RC_INVALID_IR);
    p.registers = 1; p.memory = true;
    CHECK(rc_integer_emit(&p,output,sizeof(output),&length,NULL) == RC_INVALID_IR);
    p.memory = false; insns[1].op = RC_INT_LOAD_U8;
    CHECK(rc_integer_emit(&p,output,sizeof(output),&length,NULL) == RC_MISSING_RET);
}

static uint64_t seed = UINT64_C(0x18651a4e82b75903);
static uint64_t random_value(void)
{
    seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed;
}
static void test_random(void)
{
    for (unsigned trial = 0; trial < 300; ++trial) {
        struct rc_integer_insn insns[64] = {{.op=RC_INT_ARGUMENT},
            {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1}};
        for (uint32_t n = 2; n < 8; ++n)
            insns[n] = (struct rc_integer_insn){.op=RC_INT_CONSTANT,.dst=n,.immediate=random_value()};
        for (uint32_t n = 8; n < 63; ++n) {
            if (n % 5 == 0 && n < 62)
                insns[n] = (struct rc_integer_insn){.op=RC_INT_BRANCH,.left=(uint32_t)(random_value()%8),.target=n+2};
            else
                insns[n] = (struct rc_integer_insn){.op=(enum rc_integer_op)(RC_INT_ADD+random_value()%14),
                    .dst=(uint32_t)(random_value()%8),.left=(uint32_t)(random_value()%8),.right=(uint32_t)(random_value()%8)};
        }
        insns[63] = (struct rc_integer_insn){.op=RC_INT_RETURN,.left=7};
        struct rc_integer_program *p = build(insns,64,8,2);
        struct rc_code code = {0}; rc_integer_function fn; bool allocated; size_t length;
        CHECK(rc_integer_emit(p,NULL,0,&length,NULL) == RC_OK);
        CHECK(rc_integer_map(p,length,&code,&fn,&allocated,NULL) == RC_OK);
        for (unsigned sample = 0; sample < 32; ++sample) {
            uint64_t args[] = {random_value(),random_value()}, regs[8] = {args[0],args[1]};
            for (size_t pc = 2; pc < 63; ++pc) {
                struct rc_integer_insn i = insns[pc];
                if (i.op == RC_INT_CONSTANT) regs[i.dst] = i.immediate;
                else if (i.op == RC_INT_BRANCH) { if (regs[i.left]) pc = i.target-1; }
                else regs[i.dst] = expected(i.op,regs[i.left],regs[i.right]);
            }
            verify(p,args,regs[7],fn);
        }
        CHECK(rc_code_free(&code,NULL) == RC_OK); rc_integer_program_destroy(p);
    }
}
static void test_loops_and_ownership(void)
{
    struct rc_integer_insn loop[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_CONSTANT,.dst=1,.immediate=1},
        {.op=RC_INT_BRANCH,.target=4},{.op=RC_INT_RETURN},
        {.op=RC_INT_SUB,.dst=0,.right=1},{.op=RC_INT_JUMP,.target=2}};
    struct rc_integer_program *p = build(loop,6,2,1);
    uint64_t result = 19, args[] = {10};
    CHECK(rc_integer_interpret(p,args,1,3,&result,NULL) == RC_BUDGET && result == 19);
    struct rc_runtime *r = runtime(RC_POLICY_FIXED,4096);
    rc_handle h, alias;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    CHECK(rc_runtime_register_integer(r,p,&alias,NULL) == RC_OK);
    rc_integer_program_destroy(p);
    for (unsigned n = 0; n < 20; ++n) {
        args[0] = n;
        CHECK(rc_runtime_call_integer(r,h,args,1,&result,NULL) == RC_OK && result == 0);
    }
    struct rc_runtime_stats s;
    CHECK(rc_runtime_get_stats(r,&s,NULL) == RC_OK && s.translations == 1 && s.executable_bytes == 4096);
    CHECK(rc_runtime_invalidate(r,alias,NULL) == RC_OK);
    CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    CHECK(rc_runtime_call_integer(r,h,args,1,&result,NULL) == RC_NOT_FOUND);
    for (unsigned n = 0; n < 10; ++n) CHECK(rc_runtime_call_integer(r,alias,args,1,&result,NULL) == RC_OK && result == 0);
    CHECK(rc_runtime_remove(r,alias,NULL) == RC_OK);
    CHECK(rc_runtime_get_stats(r,&s,NULL) == RC_OK && s.program_bytes == 0 && s.executable_bytes == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    struct rc_integer_insn constant[] = {{.op=RC_INT_CONSTANT,.immediate=UINT64_MAX},{.op=RC_INT_RETURN}};
    p = build(constant,2,1,0); r = runtime(RC_POLICY_FIXED,4096);
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    for (unsigned n = 0; n < 30; ++n) CHECK(rc_runtime_call_integer(r,h,NULL,0,&result,NULL) == RC_OK && result == UINT64_MAX);
    struct rc_function_stats f;
    CHECK(rc_runtime_function_stats(r,h,&f,NULL) == RC_OK && f.jitted == 29);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    r = runtime(RC_POLICY_FIXED,0);
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    for (unsigned n = 0; n < 30; ++n) CHECK(rc_runtime_call_integer(r,h,NULL,0,&result,NULL) == RC_OK && result == UINT64_MAX);
    CHECK(rc_runtime_function_stats(r,h,&f,NULL) == RC_OK && f.compilation_failures > 0 && f.jitted == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK); rc_integer_program_destroy(p);
}
static void test_capacity_and_identity(void)
{
    struct rc_runtime *r = runtime(RC_POLICY_FIXED,4096);
    struct rc_integer_insn insns[514];
    for (size_t n = 0; n < 513; ++n)
        insns[n] = (struct rc_integer_insn){.op=RC_INT_CONSTANT,.immediate=n};
    insns[513] = (struct rc_integer_insn){.op=RC_INT_RETURN};
    struct rc_integer_program *p = build(insns,514,1,0);
    rc_handle h;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    uint64_t value;
    for (unsigned n = 0; n < 3; ++n) CHECK(rc_runtime_call_integer(r,h,NULL,0,&value,NULL) == RC_OK && value == 512);
    struct rc_function_stats f;
    CHECK(rc_runtime_function_stats(r,h,&f,NULL) == RC_OK && f.last_compile_error.status == RC_CAPACITY);
    rc_integer_program_destroy(p);
    CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    rc_handle handles[32];
    for (uint32_t n = 0; n < 32; ++n) {
        struct rc_integer_insn constant[] = {{.op=RC_INT_CONSTANT,.dst=n,.immediate=n},
            {.op=RC_INT_RETURN,.left=n}};
        p = build(constant,2,32,0);
        CHECK(rc_runtime_register_integer(r,p,&handles[n],NULL) == RC_OK);
        rc_integer_program_destroy(p);
    }
    for (size_t n = 0; n < 32; ++n) {
        for (unsigned repeat = 0; repeat < 4; ++repeat)
            CHECK(rc_runtime_call_integer(r,handles[n],NULL,0,&value,NULL) == RC_OK && value == n);
    }
    struct rc_runtime_stats stats;
    CHECK(rc_runtime_get_stats(r,&stats,NULL) == RC_OK && stats.translations == 32 && stats.evictions > 0 && stats.peak_executable_bytes == 4096);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}
struct worker { struct rc_runtime *r; rc_handle h; atomic_bool *start; };
static void *work(void *data)
{
    struct worker *w = data;
    while (!atomic_load(w->start)) { }
    for (uint64_t n = 0; n < 3000; ++n) {
        uint64_t args[] = {n,UINT64_MAX}, result;
        CHECK(rc_runtime_call_integer(w->r,w->h,args,2,&result,NULL) == RC_OK && result == n-1);
    }
    return NULL;
}
static void test_concurrency(void)
{
    struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    struct rc_integer_program *p = build(insns,4,2,2);
    struct rc_runtime *r = runtime(RC_POLICY_FIXED,4096); rc_handle h;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK); rc_integer_program_destroy(p);
    atomic_bool start = false; pthread_t threads[6]; struct worker w = {r,h,&start};
    for (unsigned n = 0; n < 6; ++n) CHECK(pthread_create(&threads[n],NULL,work,&w) == 0);
    atomic_store(&start,true);
    for (unsigned n = 0; n < 500; ++n) {
        enum rc_status status = rc_runtime_clear(r,NULL); CHECK(status == RC_OK || status == RC_BUSY);
        status = rc_runtime_invalidate(r,h,NULL); CHECK(status == RC_OK || status == RC_BUSY);
    }
    for (unsigned n = 0; n < 6; ++n) CHECK(pthread_join(threads[n],NULL) == 0);
    struct rc_runtime_stats s; CHECK(rc_runtime_get_stats(r,&s,NULL) == RC_OK && s.invocations == 18000 && s.active_calls == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}
int main(void)
{
    alarm(60);
    test_emitter_validation(); test_compact_layout(); test_validation(); test_operations(); test_random(); test_loops_and_ownership(); test_capacity_and_identity(); test_concurrency();
    puts("integer builder, operations, independent differential, ABI, policies, ownership and concurrency passed");
    return 0;
}
