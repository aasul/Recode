#define _POSIX_C_SOURCE 200809L
#include "internal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); \
} } while (0)

static const uint8_t identity[] = {0x48,0x89,0xf8,0xc3};
static const uint8_t increment[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,1,0xc3};

static struct rc_runtime *create(size_t budget)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = RC_POLICY_FIXED;
    c.hot_threshold = 2;
    c.retry_calls = 2;
    c.executable_budget = budget;
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c, &r, NULL) == RC_OK);
    return r;
}

static rc_handle reg(struct rc_runtime *r, const uint8_t *bytes, size_t length)
{
    rc_handle h = 0;
    CHECK(rc_runtime_register(r, bytes, length, &h, NULL) == RC_OK && h != 0);
    return h;
}

static void call(struct rc_runtime *r, rc_handle h, uint64_t x, uint64_t expected)
{
    uint64_t value = 42;
    CHECK(rc_runtime_call(r, h, x, &value, NULL) == RC_OK && value == expected);
}

static struct rc_function_stats function_stats(struct rc_runtime *r, rc_handle h)
{
    struct rc_function_stats s;
    CHECK(rc_runtime_function_stats(r, h, &s, NULL) == RC_OK);
    return s;
}

static struct rc_runtime_stats stats(struct rc_runtime *r)
{
    struct rc_runtime_stats s;
    CHECK(rc_runtime_get_stats(r, &s, NULL) == RC_OK);
    return s;
}

static void test_interpreter(void)
{
    struct rc_insn invalid[] = {{RC_JMP,2}};
    struct rc_program p = {invalid,1};
    uint64_t value = 19;
    CHECK(rc_interpret(&p, 0, 0, &value, NULL) == RC_INVALID_IR && value == 19);
    invalid[0] = (struct rc_insn){RC_RET,0};
    CHECK(rc_interpret(&p, 0, 0, &value, NULL) == RC_UNINITIALIZED);
    struct rc_insn insns[] = {{RC_CONST,UINT32_MAX},{RC_ADD,UINT32_MAX},{RC_RET,0}};
    p = (struct rc_program){insns,3};
    CHECK(rc_interpret(&p, 0, 2, &value, NULL) == RC_BUDGET && value == 19);
    CHECK(rc_interpret(&p, 0, 3, &value, NULL) == RC_OK && value == UINT64_C(4294967294));
    CHECK(rc_interpret(&p, 0, 0, NULL, NULL) == RC_ARGUMENT);
    CHECK(rc_saturating_add(UINT64_MAX - 1, 2) == UINT64_MAX);
    CHECK(rc_saturating_add(1, UINT64_MAX) == UINT64_MAX);
    CHECK(rc_saturating_add(UINT64_MAX, 0) == UINT64_MAX);
}

static void test_tiers_and_identity(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = create(2 * page);
    size_t baseline = stats(r).metadata_bytes;
    uint8_t source[] = {0x48,0x89,0xf8,0xc3};
    rc_handle a = reg(r, source, sizeof(source));
    source[0] = 0xff;
    for (uint64_t i = 0; i < 20; ++i)
        call(r, a, i, i);
    struct rc_function_stats s = function_stats(r, a);
    CHECK(s.invocations == 20 && s.interpreted == 1 && s.jitted == 19);
    CHECK(s.compilation_attempts == 1 && s.compilations == 1 && s.compilation_failures == 0);
    CHECK(s.cache_hits == 18 && s.cache_misses == 2 && s.generated_size == 8);
    CHECK(s.interpreted_samples == 1 && s.compiled_samples >= 8);
    CHECK(stats(r).executable_bytes == page && stats(r).program_bytes == 2 * sizeof(struct rc_insn));
    /* An alternate source encoding has the same canonical IR. */
    const uint8_t alternate[] = {0x48,0x8b,0xc7,0xc3};
    rc_handle alias = reg(r, alternate, sizeof(alternate));
    CHECK(stats(r).translations == 1);
    call(r, alias, UINT64_MAX, UINT64_MAX);
    call(r, alias, 7, 7);
    s = function_stats(r, alias);
    CHECK(s.interpreted == 1 && s.jitted == 1 && s.compilation_attempts == 0 && s.cache_hits == 1);
    rc_handle b = reg(r, increment, sizeof(increment));
    call(r, b, UINT64_MAX, 0); call(r, b, UINT64_MAX, 0);
    CHECK(stats(r).translations == 2 && stats(r).executable_bytes == 2 * page);
    CHECK(rc_runtime_invalidate(r, alias, NULL) == RC_OK);
    CHECK(stats(r).executable_bytes == page);
    call(r, a, 8, 8);
    CHECK(rc_runtime_remove(r, a, NULL) == RC_OK);
    CHECK(rc_runtime_call(r, a, 0, &(uint64_t){0}, NULL) == RC_NOT_FOUND);
    CHECK(rc_runtime_remove(r, a, NULL) == RC_NOT_FOUND);
    CHECK(rc_runtime_remove(r, alias, NULL) == RC_OK);
    CHECK(rc_runtime_remove(r, b, NULL) == RC_OK);
    struct rc_runtime_stats all = stats(r);
    CHECK(all.functions == 0 && all.translations == 0 && all.program_bytes == 0);
    CHECK(all.executable_bytes == 0 && all.metadata_bytes == baseline && all.active_calls == 0);
    for (unsigned i = 0; i < 1000; ++i) {
        rc_handle h = reg(r, identity, sizeof(identity));
        CHECK(h > b);
        call(r, h, i, i); call(r, h, i, i);
        CHECK(rc_runtime_remove(r, h, NULL) == RC_OK);
    }
    CHECK(stats(r).metadata_bytes == baseline && stats(r).executable_bytes == 0);
    CHECK(rc_runtime_destroy(&r, NULL) == RC_OK && r == NULL);
}

static void test_large_mapping(void)
{
    const size_t count = 700;
    size_t length = 3 + 6 * count + 1, page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *bytes = malloc(length);
    CHECK(bytes != NULL);
    bytes[0] = 0x48; bytes[1] = 0x89; bytes[2] = 0xf8;
    for (size_t i = 0; i < count; ++i) {
        size_t at = 3 + i * 6;
        bytes[at] = 0x48; bytes[at+1] = 0x05; bytes[at+2] = 1;
        bytes[at+3] = 0; bytes[at+4] = 0; bytes[at+5] = 0;
    }
    bytes[length-1] = 0xc3;
    for (unsigned mode = 0; mode < 2; ++mode) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = RC_POLICY_FIXED; c.hot_threshold = 2; c.optimize = false;
        size_t needed = (length + 4 + page - 1) / page * page;
        c.executable_budget = needed - (mode == 0 ? 1 : 0);
        struct rc_runtime *r = NULL;
        CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
        rc_handle h = reg(r,bytes,length);
        call(r,h,UINT64_MAX,count-1); call(r,h,UINT64_MAX,count-1);
        if (mode == 0) {
            CHECK(function_stats(r,h).last_compile_error.status == RC_CAPACITY);
            CHECK(stats(r).executable_bytes == 0);
        } else {
            CHECK(function_stats(r,h).generated_size == length+4);
            CHECK(stats(r).executable_bytes == needed && function_stats(r,h).jitted == 1);
        }
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    }
    free(bytes);
}

static void test_pressure(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = create(page);
    rc_handle a = reg(r, identity, sizeof(identity));
    rc_handle b = reg(r, increment, sizeof(increment));
    for (unsigned i = 0; i < 1000; ++i) {
        rc_handle h = i % 2 == 0 ? a : b;
        for (unsigned j = 0; j < 4; ++j)
            call(r, h, i, i + i % 2);
        CHECK(stats(r).executable_bytes <= page);
    }
    CHECK(stats(r).evictions > 100 && stats(r).peak_executable_bytes == page);
    CHECK(rc_runtime_clear(r, NULL) == RC_OK && stats(r).executable_bytes == 0);
    CHECK(rc_runtime_destroy(&r, NULL) == RC_OK);
    r = create(page - 1);
    a = reg(r, identity, sizeof(identity));
    for (unsigned i = 0; i < 100; ++i)
        call(r, a, i, i);
    struct rc_function_stats s = function_stats(r, a);
    CHECK(s.jitted == 0 && s.interpreted == 100 && s.compilation_failures > 0);
    CHECK(s.compilation_attempts < 10 && s.last_compile_error.status == RC_CAPACITY);
    CHECK(stats(r).capacity_failures == s.compilation_failures && stats(r).peak_executable_bytes == 0);
    CHECK(rc_runtime_destroy(&r, NULL) == RC_OK);
    /* LRU hits protect A, so C evicts B. */
    r = create(2 * page);
    a = reg(r, identity, sizeof(identity)); b = reg(r, increment, sizeof(increment));
    const uint8_t constant[] = {0xb8,7,0,0,0,0xc3};
    rc_handle c = reg(r, constant, sizeof(constant));
    call(r,a,1,1); call(r,a,1,1); call(r,b,1,2); call(r,b,1,2); call(r,a,1,1);
    call(r,c,1,7); call(r,c,1,7); call(r,a,1,1);
    CHECK(function_stats(r,a).compilations == 1 && function_stats(r,a).jitted == 3);
    CHECK(stats(r).evictions == 1);
    CHECK(rc_runtime_destroy(&r, NULL) == RC_OK);
}

struct worker {
    struct rc_runtime *runtime;
    rc_handle handle;
    unsigned count;
    uint64_t input, expected;
    atomic_bool *start;
};

static void *execute(void *data)
{
    struct worker *w = data;
    while (!atomic_load(w->start)) { }
    for (unsigned i = 0; i < w->count; ++i)
        call(w->runtime, w->handle, w->input, w->expected);
    return NULL;
}

static void test_threads(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = create(page);
    rc_handle a = reg(r, identity, sizeof(identity));
    rc_handle b = reg(r, increment, sizeof(increment));
    pthread_t threads[6];
    struct worker workers[6];
    atomic_bool start = false;
    for (unsigned i = 0; i < 6; ++i) {
        workers[i] = (struct worker){r, i < 3 ? a : b, 10000, 11, i < 3 ? 11 : 12, &start};
        CHECK(pthread_create(&threads[i], NULL, execute, &workers[i]) == 0);
    }
    atomic_store(&start, true);
    for (unsigned i = 0; i < 1000; ++i) {
        enum rc_status status = rc_runtime_clear(r, NULL);
        CHECK(status == RC_OK || status == RC_BUSY);
        struct rc_runtime_stats s = stats(r);
        CHECK(s.executable_bytes <= page);
        rc_handle h = reg(r, identity, sizeof(identity));
        CHECK(rc_runtime_remove(r, h, NULL) == RC_OK);
    }
    for (unsigned i = 0; i < 6; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    struct rc_function_stats sa = function_stats(r,a), sb = function_stats(r,b);
    CHECK(sa.invocations == 30000 && sb.invocations == 30000);
    CHECK(sa.interpreted + sa.jitted == 30000 && sb.interpreted + sb.jitted == 30000);
    CHECK(stats(r).invocations == 60000 && stats(r).active_calls == 0);
    CHECK(rc_runtime_destroy(&r, NULL) == RC_OK);
    /* With room, six racing callers share one compilation. */
    r = create(page);
    a = reg(r, identity, sizeof(identity));
    atomic_store(&start, false);
    for (unsigned i = 0; i < 6; ++i) {
        workers[i] = (struct worker){r, a, 1000, 11, 11, &start};
        CHECK(pthread_create(&threads[i], NULL, execute, &workers[i]) == 0);
    }
    atomic_store(&start, true);
    for (unsigned i = 0; i < 6; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    CHECK(function_stats(r,a).compilations == 1);
    CHECK(rc_runtime_destroy(&r, NULL) == RC_OK);
}

static void test_contract(void)
{
    struct rc_runtime *r = NULL;
    struct rc_runtime_config c = rc_runtime_default_config();
    c.sample_period = 0;
    CHECK(rc_runtime_create(&c, &r, NULL) == RC_ARGUMENT && r == NULL);
    c = rc_runtime_default_config(); c.policy = RC_POLICY_INTERPRET;
    CHECK(rc_runtime_create(&c, &r, NULL) == RC_OK);
    CHECK(rc_runtime_create(&c, &r, NULL) == RC_ARGUMENT);
    rc_handle h = 19;
    const uint8_t invalid[] = {0xc3};
    CHECK(rc_runtime_register(r, invalid, sizeof(invalid), &h, NULL) == RC_UNINITIALIZED && h == 19);
    h = reg(r, identity, sizeof(identity));
    for (unsigned i = 0; i < 100; ++i)
        call(r,h,i,i);
    CHECK(function_stats(r,h).compilation_attempts == 0 && function_stats(r,h).interpreted == 100);
    CHECK(rc_runtime_invalidate(r,0,NULL) == RC_NOT_FOUND);
    CHECK(rc_runtime_function_stats(r,0,&(struct rc_function_stats){0},NULL) == RC_NOT_FOUND);
    CHECK(rc_runtime_call(r,h,0,NULL,NULL) == RC_ARGUMENT);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_ARGUMENT);
}

int main(void)
{
    test_interpreter(); test_tiers_and_identity(); test_large_mapping(); test_pressure(); test_threads(); test_contract();
    puts("interpreter, tiers, identity, budget, LRU, ownership and concurrent calls passed");
    return 0;
}
