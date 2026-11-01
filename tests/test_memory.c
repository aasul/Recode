#define _GNU_SOURCE
#include "integer.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
enum rc_status rc_test_integer_view_abi(rc_integer_view_function fn, uint64_t a, uint64_t b,
                                        const void *data, size_t length,
                                        struct rc_integer_result *out, uint64_t *ok);

static struct rc_integer_program *build(enum rc_integer_op op, uint64_t salt)
{
    struct rc_builder *b = NULL;
    struct rc_integer_program *p = NULL;
    const struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
        {.op=op,.left=0}, {.op=RC_INT_CONSTANT,.dst=1,.immediate=salt},
        {.op=RC_INT_XOR,.right=1}, {.op=RC_INT_RETURN}};
    CHECK(rc_builder_create(2,1,&b,NULL) == RC_OK);
    for (size_t n = 0; n < 5; ++n) CHECK(rc_builder_append(b,insns[n],NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK);
    rc_builder_destroy(b);
    return p;
}
static struct rc_runtime *runtime(enum rc_policy policy)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = policy; c.hot_threshold = 2; c.retry_calls = 2;
    c.executable_budget = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    return r;
}
static uint64_t random_value(uint64_t *state)
{
    *state ^= *state << 13; *state ^= *state >> 7; *state ^= *state << 17;
    return *state;
}
static void test_reads(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *guard = mmap(NULL,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(guard != MAP_FAILED && mprotect(guard+page,page,PROT_READ|PROT_WRITE) == 0);
    uint8_t *data = guard+2*page-33; /* Misaligned and ending at a guard page. */
    for (size_t n = 0; n < 33; ++n) data[n] = (uint8_t)(n*19+137);
    for (enum rc_integer_op op = RC_INT_LOAD_U8; op <= RC_INT_LOAD_U64; ++op) {
        struct rc_integer_program *p = build(op,0);
        struct rc_code code = {0}; rc_integer_function raw; bool allocated; size_t length;
        CHECK(rc_integer_emit(p,NULL,0,&length,NULL) == RC_OK);
        uint8_t bytes[256]; memset(bytes,0xa5,sizeof(bytes));
        CHECK(rc_integer_emit(p,bytes,length-1,&length,NULL) == RC_CAPACITY && bytes[0] == 0xa5);
        CHECK(rc_integer_emit(p,bytes,sizeof(bytes),&length,NULL) == RC_OK && bytes[length] == 0xa5);
        CHECK(rc_integer_map(p,length,&code,&raw,&allocated,NULL) == RC_OK);
        rc_integer_view_function native; memcpy(&native,&raw,sizeof(native));
        struct rc_runtime *r = runtime(RC_POLICY_FIXED); rc_handle h;
        CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
        size_t width = (size_t)1 << (op-RC_INT_LOAD_U8);
        uint64_t seed = UINT64_C(0x56f0307df9876191);
        for (unsigned trial = 0; trial < 3000; ++trial) {
            uint64_t offset = trial < 36 ? trial : random_value(&seed);
            if (trial >= 36 && trial % 2) offset %= 40;
            size_t size = trial < 36 ? 33 : (size_t)(random_value(&seed)%34);
            struct rc_memory_view view = {data,size};
            enum rc_status expected = offset <= size && width <= size-(size_t)offset ? RC_OK : RC_BOUNDS;
            uint64_t reference = 0;
            if (expected == RC_OK)
                for (size_t n = width; n-- > 0;) reference = reference*256 + data[(size_t)offset+n];
            uint64_t result = 91, abi = 0; struct rc_error error;
            CHECK(rc_integer_interpret_view(p,&offset,1,&view,100,&result,&error) == expected);
            CHECK(result == (expected == RC_OK ? reference : 91));
            CHECK(error.status == expected && error.offset == (expected == RC_OK ? 0u : 1u));
            struct rc_integer_result out = {91,99};
            CHECK(rc_test_integer_view_abi(native,offset,0,view.data,view.length,&out,&abi) == expected && abi == 1);
            CHECK(out.value == (expected == RC_OK ? reference : 91));
            if (expected != RC_OK) CHECK(out.offset == 1);
            result = 91;
            CHECK(rc_runtime_call_integer_view(r,h,&offset,1,&view,&result,&error) == expected);
            CHECK(result == (expected == RC_OK ? reference : 91));
            CHECK(error.offset == (expected == RC_OK ? 0u : 1u));
        }
        uint64_t offset = 0, result = 91;
        struct rc_memory_view empty = {NULL,0}, bad = {NULL,1};
        CHECK(rc_integer_interpret_view(p,&offset,1,NULL,0,&result,NULL) == RC_BOUNDS && result == 91);
        CHECK(rc_integer_interpret_view(p,&offset,1,&empty,0,&result,NULL) == RC_BOUNDS && result == 91);
        CHECK(rc_integer_interpret_view(p,&offset,1,&bad,0,&result,NULL) == RC_ARGUMENT && result == 91);
        CHECK(rc_runtime_call_integer_view(r,h,&offset,1,&bad,&result,NULL) == RC_ARGUMENT && result == 91);
        CHECK(rc_runtime_call_integer(r,h,&offset,1,&result,NULL) == RC_BOUNDS && result == 91);
        CHECK(rc_integer_interpret(p,&offset,1,0,&result,NULL) == RC_BOUNDS && result == 91);
        struct rc_function_stats stats;
        CHECK(rc_runtime_function_stats(r,h,&stats,NULL) == RC_OK && stats.jitted == 3000);
        CHECK(rc_code_free(&code,NULL) == RC_OK);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK); rc_integer_program_destroy(p);
    }
    CHECK(munmap(guard,3*page) == 0);
}
static void test_validation_and_control(void)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    CHECK(rc_builder_create(3,1,&b,NULL) == RC_OK);
    const struct rc_integer_insn invalid[] = {{.op=RC_INT_LOAD_U8,.dst=3},
        {.op=RC_INT_LOAD_U16,.left=3}, {.op=RC_INT_LOAD_U32,.right=1},
        {.op=RC_INT_LOAD_U64,.immediate=1}, {.op=RC_INT_LOAD_U8,.target=1}};
    for (size_t n = 0; n < 5; ++n) CHECK(rc_builder_append(b,invalid[n],NULL) == RC_INVALID_IR);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_LOAD_U8},NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_UNINITIALIZED && p == NULL);
    rc_builder_destroy(b); b = NULL;
    /* Skip memory on one arm; repeatedly load on a backedge on the other. */
    const struct rc_integer_insn loop[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_CONSTANT,.dst=1,.immediate=1},
        {.op=RC_INT_BRANCH,.target=4}, {.op=RC_INT_RETURN},
        {.op=RC_INT_SUB,.right=1}, {.op=RC_INT_LOAD_U8,.dst=2},
        {.op=RC_INT_JUMP,.target=2}};
    CHECK(rc_builder_create(3,1,&b,NULL) == RC_OK);
    for (size_t n = 0; n < 7; ++n) CHECK(rc_builder_append(b,loop[n],NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK); rc_builder_destroy(b);
    struct rc_runtime *r = runtime(RC_POLICY_FIXED); rc_handle h;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    const uint8_t data[] = {7,8,9}; struct rc_memory_view view = {data,3};
    for (uint64_t arg = 0; arg < 6; ++arg) {
        uint64_t a = 91, c = 91; struct rc_error e1, e2;
        enum rc_status status = rc_integer_interpret_view(p,&arg,1,&view,100,&a,&e1);
        CHECK(rc_runtime_call_integer_view(r,h,&arg,1,&view,&c,&e2) == status);
        CHECK(a == c && e1.offset == e2.offset && status == (arg > 3 ? RC_BOUNDS : RC_OK));
    }
    uint64_t arg = 0, value = 91;
    CHECK(rc_runtime_call_integer(r,h,&arg,1,&value,NULL) == RC_OK && value == 0);
    arg = UINT64_MAX;
    CHECK(rc_integer_interpret_view(p,&arg,1,&view,1,&value,NULL) == RC_BUDGET && value == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK); rc_integer_program_destroy(p);
}
struct worker { struct rc_runtime *r; rc_handle h; unsigned id; atomic_bool *start; };
static void *work(void *context)
{
    struct worker *w = context;
    while (!atomic_load(w->start)) { }
    for (unsigned n = 0; n < 3000; ++n) {
        uint8_t data[9]; memset(data,(int)(n+w->id),sizeof(data));
        struct rc_memory_view view = {data+1,8}; uint64_t arg = n%2 ? 0 : UINT64_MAX, result = 91;
        enum rc_status status = rc_runtime_call_integer_view(w->r,w->h,&arg,1,&view,&result,NULL);
        if (arg == 0) CHECK(status == RC_OK && result == UINT64_C(0x0101010101010101)*data[1]);
        else CHECK(status == RC_BOUNDS && result == 91);
    }
    return NULL;
}
static void test_concurrency_and_eviction(void)
{
    struct rc_integer_program *p = build(RC_INT_LOAD_U64,0);
    struct rc_runtime *r = runtime(RC_POLICY_FIXED); rc_handle h, alias;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    CHECK(rc_runtime_register_integer(r,p,&alias,NULL) == RC_OK); rc_integer_program_destroy(p);
    atomic_bool start = false; struct worker workers[4]; pthread_t threads[4];
    for (unsigned n = 0; n < 4; ++n) {
        workers[n] = (struct worker){r,n%2 ? h : alias,n,&start};
        CHECK(pthread_create(&threads[n],NULL,work,&workers[n]) == 0);
    }
    atomic_store(&start,true);
    for (unsigned n = 0; n < 200; ++n) {
        p = build(RC_INT_LOAD_U64,n+1); rc_handle other;
        CHECK(rc_runtime_register_integer(r,p,&other,NULL) == RC_OK); rc_integer_program_destroy(p);
        uint64_t arg = 0, result; uint8_t data[8] = {0}; struct rc_memory_view view = {data,8};
        for (unsigned repeat = 0; repeat < 3; ++repeat)
            CHECK(rc_runtime_call_integer_view(r,other,&arg,1,&view,&result,NULL) == RC_OK && result == n+1);
        CHECK(rc_runtime_remove(r,other,NULL) == RC_OK);
        enum rc_status status = rc_runtime_invalidate(r,h,NULL); CHECK(status == RC_OK || status == RC_BUSY);
    }
    for (unsigned n = 0; n < 4; ++n) CHECK(pthread_join(threads[n],NULL) == 0);
    CHECK(rc_runtime_clear(r,NULL) == RC_OK);
    struct rc_runtime_stats stats;
    CHECK(rc_runtime_get_stats(r,&stats,NULL) == RC_OK && stats.executable_bytes == 0 && stats.active_calls == 0);
    CHECK(stats.translations == 1 && stats.functions == 2);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}
int main(void)
{
    test_reads(); test_validation_and_control(); test_concurrency_and_eviction();
    puts("bounded reads, guard pages, differential errors, ABI, ownership and concurrent views passed");
    return 0;
}
