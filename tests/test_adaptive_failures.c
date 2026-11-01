#define _GNU_SOURCE
#include "recode.h"
#include "recode_integer.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); \
} } while (0)

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void __real_free(void *p);
void *__real_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset);
int __real_mprotect(void *address, size_t size, int protection);
int __real_munmap(void *address, size_t size);
int __real_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes);
int __real_clock_gettime(clockid_t id, struct timespec *t);
static size_t malloc_calls, fail_at, live_bytes, live_maps, map_calls, live_allocations;
static bool fail_map, fail_protect, fail_release, fake_clock, fail_clock, fail_mutex;
static uint64_t clock_value = 1, clock_step = 1000;
static pthread_mutex_t barrier = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool blocked, resume_call;
static _Thread_local bool block_clock;
static _Thread_local unsigned skip_clocks;

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
    return fail_mutex ? EAGAIN : __real_pthread_mutex_init(mutex,attributes);
}

void *__wrap_malloc(size_t size)
{
    ++malloc_calls;
    void *p = malloc_calls == fail_at ? NULL : __real_malloc(size);
    if (p != NULL) ++live_allocations;
    return p;
}

void *__wrap_calloc(size_t count, size_t size)
{
    ++malloc_calls;
    void *p = malloc_calls == fail_at ? NULL : __real_calloc(count, size);
    if (p != NULL) ++live_allocations;
    return p;
}

void __wrap_free(void *p)
{
    if (p != NULL) { CHECK(live_allocations != 0); --live_allocations; }
    __real_free(p);
}

void *__wrap_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    CHECK(protection == (PROT_READ | PROT_WRITE));
    ++map_calls;
    if (fail_map) { errno = ENOMEM; return MAP_FAILED; }
    void *p = __real_mmap(address,size,protection,flags,fd,offset);
    if (p != MAP_FAILED) { live_bytes += size; ++live_maps; }
    return p;
}

int __wrap_mprotect(void *address, size_t size, int protection)
{
    CHECK(protection == (PROT_READ | PROT_EXEC));
    if (fail_protect) { errno = EACCES; return -1; }
    return __real_mprotect(address,size,protection);
}

int __wrap_munmap(void *address, size_t size)
{
    if (fail_release) { errno = EINVAL; return -1; }
    int status = __real_munmap(address,size);
    if (status == 0) { CHECK(live_bytes >= size && live_maps != 0); live_bytes -= size; --live_maps; }
    return status;
}

int __wrap_clock_gettime(clockid_t id, struct timespec *t)
{
    if (block_clock && skip_clocks != 0) {
        --skip_clocks;
    } else if (block_clock) {
        block_clock = false;
        pthread_mutex_lock(&barrier);
        blocked = true;
        pthread_cond_broadcast(&condition);
        while (!resume_call)
            pthread_cond_wait(&condition,&barrier);
        pthread_mutex_unlock(&barrier);
    }
    if (fail_clock) { errno = EIO; return -1; }
    if (!fake_clock)
        return __real_clock_gettime(id,t);
    clock_value += clock_step;
    t->tv_sec = (time_t)(clock_value / UINT64_C(1000000000));
    t->tv_nsec = (long)(clock_value % UINT64_C(1000000000));
    return 0;
}

static const uint8_t bytes[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,1,0xc3};

static struct rc_runtime *create(enum rc_policy policy)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = policy; c.hot_threshold = 2; c.retry_calls = 8;
    c.executable_budget = (size_t)sysconf(_SC_PAGESIZE);
    c.initial_compile_ns = 1000; c.initial_native_ns = 10;
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    return r;
}

static rc_handle reg(struct rc_runtime *r)
{
    rc_handle h;
    CHECK(rc_runtime_register(r,bytes,sizeof(bytes),&h,NULL) == RC_OK);
    return h;
}

static void call(struct rc_runtime *r, rc_handle h)
{
    uint64_t value;
    struct rc_error error = {RC_SYSTEM,99,99};
    CHECK(rc_runtime_call(r,h,UINT64_MAX,&value,&error) == RC_OK && value == 0);
    CHECK(error.status == RC_OK && error.system_errno == 0);
}

static struct rc_function_stats function_stats(struct rc_runtime *r, rc_handle h)
{
    struct rc_function_stats s;
    CHECK(rc_runtime_function_stats(r,h,&s,NULL) == RC_OK);
    return s;
}

static void accounted(struct rc_runtime *r)
{
    struct rc_runtime_stats s;
    CHECK(rc_runtime_get_stats(r,&s,NULL) == RC_OK);
    CHECK(s.executable_bytes == live_bytes && live_bytes <= (size_t)sysconf(_SC_PAGESIZE));
    CHECK(live_bytes % (size_t)sysconf(_SC_PAGESIZE) == 0);
}

static void test_allocations(void)
{
    struct rc_runtime *r = NULL;
    struct rc_runtime_config c = rc_runtime_default_config();
    malloc_calls = 0; fail_at = 1;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_MEMORY && r == NULL);
    fail_at = 0; fail_mutex = true;
    struct rc_error error;
    CHECK(rc_runtime_create(&c,&r,&error) == RC_SYSTEM && r == NULL);
    CHECK(error.system_errno == EAGAIN && live_allocations == 0);
    fail_mutex = false; r = create(RC_POLICY_FIXED);
    malloc_calls = 0;
    rc_handle h = reg(r);
    size_t allocations = malloc_calls;
    CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    for (size_t i = 1; i <= allocations; ++i) {
        malloc_calls = 0; fail_at = i; h = 91;
        CHECK(rc_runtime_register(r,bytes,sizeof(bytes),&h,NULL) == RC_MEMORY && h == 91);
        struct rc_runtime_stats s;
        CHECK(rc_runtime_get_stats(r,&s,NULL) == RC_OK && s.functions == 0 && s.program_bytes == 0);
    }
    fail_at = 0; h = reg(r); call(r,h);
    malloc_calls = 0; call(r,h);
    allocations = malloc_calls;
    CHECK(allocations > 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_maps == 0);
    for (size_t i = 1; i <= allocations; ++i) {
        r = create(RC_POLICY_FIXED); h = reg(r); call(r,h);
        malloc_calls = 0; fail_at = i;
        call(r,h); /* Any compilation failure must return the interpreted answer. */
        fail_at = 0;
        struct rc_function_stats s = function_stats(r,h);
        CHECK(s.compilation_attempts == 1 && s.compilation_failures == 1 && s.jitted == 0);
        CHECK(s.last_compile_error.status == RC_MEMORY);
        accounted(r);
        for (unsigned j = 0; j < 20; ++j)
            call(r,h);
        CHECK(function_stats(r,h).compilations == 1);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_maps == 0);
    }
}

static void test_system_failures(void)
{
    /* Find a post-mmap emission allocation and force rollback failure there. */
    bool retained = false;
    for (size_t i = 1; i < 32 && !retained; ++i) {
        struct rc_runtime *r = create(RC_POLICY_FIXED);
        rc_handle h = reg(r); call(r,h);
        malloc_calls = 0; fail_at = i; fail_release = true;
        call(r,h);
        fail_at = 0;
        if (live_maps != 0 && function_stats(r,h).compilation_failures != 0) {
            retained = true;
            CHECK(function_stats(r,h).last_compile_error.system_errno == EINVAL);
            accounted(r);
        }
        fail_release = false;
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_maps == 0 && live_allocations == 0);
    }
    CHECK(retained);
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct rc_runtime *r = create(RC_POLICY_FIXED);
        rc_handle h = reg(r); call(r,h);
        fail_map = mode == 0; fail_protect = mode != 0; fail_release = mode == 2;
        call(r,h);
        struct rc_function_stats s = function_stats(r,h);
        CHECK(s.compilation_failures == 1 && s.interpreted == 2 && s.jitted == 0);
        CHECK(s.last_compile_error.status == RC_SYSTEM);
        CHECK(s.last_compile_error.system_errno == (mode == 0 ? ENOMEM : (mode == 1 ? EACCES : EINVAL)));
        accounted(r);
        CHECK(live_maps == (mode == 2 ? 1U : 0U));
        struct rc_runtime_stats peak;
        CHECK(rc_runtime_get_stats(r,&peak,NULL) == RC_OK);
        CHECK(peak.peak_executable_bytes == (mode == 0 ? 0 : (size_t)sysconf(_SC_PAGESIZE)));
        size_t attempts = map_calls;
        for (unsigned j = 0; j < 4; ++j)
            call(r,h);
        CHECK(map_calls == attempts);
        if (mode == 2) {
            CHECK(rc_runtime_remove(r,h,NULL) == RC_SYSTEM);
            CHECK(rc_runtime_destroy(&r,NULL) == RC_SYSTEM && r != NULL);
            accounted(r);
        }
        fail_map = false; fail_protect = false; fail_release = false;
        for (unsigned j = 0; j < 30; ++j)
            call(r,h);
        CHECK(function_stats(r,h).jitted > 0);
        fail_release = true;
        CHECK(rc_runtime_invalidate(r,h,NULL) == RC_SYSTEM);
        struct rc_runtime_stats all;
        CHECK(rc_runtime_get_stats(r,&all,NULL) == RC_OK);
        CHECK(all.release_failures > 0 && all.last_cache_error.system_errno == EINVAL);
        accounted(r);
        /* A failed invalidation must disable native execution. */
        uint64_t jitted = function_stats(r,h).jitted;
        call(r,h);
        CHECK(function_stats(r,h).jitted == jitted);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_SYSTEM && r != NULL);
        fail_release = false;
        CHECK(rc_runtime_clear(r,NULL) == RC_OK && live_maps == 0);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    }
    /* A failed victim release cannot create space for a second mapping. */
    struct rc_runtime *r = create(RC_POLICY_FIXED);
    rc_handle a = reg(r); call(r,a); call(r,a);
    const uint8_t other[] = {0x48,0x89,0xf8,0xc3};
    rc_handle b;
    CHECK(rc_runtime_register(r,other,sizeof(other),&b,NULL) == RC_OK);
    uint64_t value;
    CHECK(rc_runtime_call(r,b,7,&value,NULL) == RC_OK && value == 7);
    fail_release = true;
    CHECK(rc_runtime_call(r,b,7,&value,NULL) == RC_OK && value == 7);
    CHECK(function_stats(r,b).compilation_failures == 1 && live_maps == 1);
    accounted(r);
    fail_release = false;
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_maps == 0);
}

static void test_retry_isolation(void)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = RC_POLICY_FIXED; c.hot_threshold = 2; c.retry_calls = 8;
    c.executable_budget = 2 * (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    const uint8_t identity[] = {0x48,0x89,0xf8,0xc3};
    rc_handle cold = reg(r), hot;
    CHECK(rc_runtime_register(r,identity,sizeof(identity),&hot,NULL) == RC_OK);
    uint64_t value;
    CHECK(rc_runtime_call(r,hot,7,&value,NULL) == RC_OK && value == 7);
    CHECK(rc_runtime_call(r,hot,7,&value,NULL) == RC_OK && value == 7);
    call(r,cold);
    fail_map = true;
    call(r,cold);
    CHECK(function_stats(r,cold).compilation_attempts == 1);
    for (unsigned i = 0; i < 100; ++i)
        CHECK(rc_runtime_call(r,hot,7,&value,NULL) == RC_OK && value == 7);
    call(r,cold);
    CHECK(function_stats(r,cold).compilation_attempts == 1);
    CHECK(function_stats(r,hot).jitted == 101);
    rc_handle alias = reg(r);
    call(r,alias); call(r,alias);
    CHECK(function_stats(r,alias).compilation_attempts == 0);
    fail_map = false;
    for (unsigned i = 0; i < 20; ++i)
        call(r,cold);
    CHECK(function_stats(r,cold).compilations == 1);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}

static void test_policy(void)
{
    fake_clock = true; clock_step = 1000;
    struct rc_runtime *r = create(RC_POLICY_ADAPTIVE);
    rc_handle h = reg(r);
    for (unsigned i = 0; i < 8; ++i)
        call(r,h);
    CHECK(function_stats(r,h).compilation_attempts == 0);
    clock_step = 10;
    for (unsigned i = 0; i < 40; ++i)
        call(r,h);
    struct rc_function_stats s = function_stats(r,h);
    CHECK(s.compilations == 1 && s.jitted == 40 && s.interpreted == 8);
    CHECK(s.compiled_ns > 0 && s.compilation_ns > 0 && s.policy_postponed > 0);
    /* One sampled scheduling outlier cannot retire the native tier. */
    clock_step = 100000;
    for (unsigned i = 0; i < 16; ++i)
        call(r,h);
    clock_step = 10;
    for (unsigned i = 0; i < 16; ++i)
        call(r,h);
    CHECK(function_stats(r,h).compilations == 1 && live_maps == 1);
    /* A now-slower native tier is retired and not retried on every call. */
    clock_step = 100000;
    for (unsigned i = 0; i < 130; ++i)
        call(r,h);
    CHECK(function_stats(r,h).policy_postponed > s.policy_postponed);
    CHECK(function_stats(r,h).interpreted > 8 && live_maps == 0);
    CHECK(function_stats(r,h).compilation_attempts < 8);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    struct rc_runtime_config c = rc_runtime_default_config();
    c.expected_reuse = 1; c.initial_compile_ns = UINT64_C(1000000000000);
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    h = reg(r); clock_step = 1000;
    for (unsigned i = 0; i < 100; ++i)
        call(r,h);
    CHECK(function_stats(r,h).compilation_attempts == 0 && function_stats(r,h).policy_postponed > 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    r = create(RC_POLICY_ADAPTIVE); h = reg(r);
    for (unsigned i = 0; i < 8; ++i)
        call(r,h);
    clock_value += UINT64_C(1000000000000);
    call(r,h);
    CHECK(function_stats(r,h).compilation_attempts == 0);
    fail_clock = true;
    for (unsigned i = 0; i < 20; ++i)
        call(r,h);
    CHECK(function_stats(r,h).compilation_attempts == 0 && function_stats(r,h).timing_failures > 0);
    fail_clock = false; fake_clock = false;
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}

struct running { struct rc_runtime *runtime; rc_handle handle; bool interpreted; };
static void *pinned_call(void *data)
{
    struct running *run = data;
    block_clock = true;
    skip_clocks = run->interpreted ? 1 : 0;
    call(run->runtime,run->handle);
    return NULL;
}

static void test_pins(void)
{
    blocked = false; resume_call = false;
    struct rc_runtime *r = create(RC_POLICY_FIXED);
    rc_handle a = reg(r); call(r,a); call(r,a);
    struct running run = {r,a,false};
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,pinned_call,&run) == 0);
    pthread_mutex_lock(&barrier);
    while (!blocked)
        pthread_cond_wait(&condition,&barrier);
    pthread_mutex_unlock(&barrier);
    /* Worker stopped after lookup/pin and unlock, before native invocation. */
    CHECK(rc_runtime_remove(r,a,NULL) == RC_BUSY);
    CHECK(rc_runtime_invalidate(r,a,NULL) == RC_BUSY);
    CHECK(rc_runtime_clear(r,NULL) == RC_BUSY);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_BUSY && r != NULL);
    const uint8_t other[] = {0x48,0x89,0xf8,0xc3};
    rc_handle b;
    CHECK(rc_runtime_register(r,other,sizeof(other),&b,NULL) == RC_OK);
    uint64_t value;
    for (unsigned i = 0; i < 3; ++i)
        CHECK(rc_runtime_call(r,b,7,&value,NULL) == RC_OK && value == 7);
    CHECK(function_stats(r,b).last_compile_error.status == RC_CAPACITY);
    CHECK(live_maps == 1); accounted(r);
    pthread_mutex_lock(&barrier);
    resume_call = true;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&barrier);
    CHECK(pthread_join(thread,NULL) == 0);
    CHECK(rc_runtime_remove(r,a,NULL) == RC_OK && live_maps == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    r = create(RC_POLICY_INTERPRET); a = reg(r);
    run = (struct running){r,a,true};
    blocked = false; resume_call = false;
    CHECK(pthread_create(&thread,NULL,pinned_call,&run) == 0);
    pthread_mutex_lock(&barrier);
    while (!blocked)
        pthread_cond_wait(&condition,&barrier);
    pthread_mutex_unlock(&barrier);
    CHECK(rc_runtime_remove(r,a,NULL) == RC_BUSY);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_BUSY);
    CHECK(rc_runtime_clear(r,NULL) == RC_OK);
    pthread_mutex_lock(&barrier);
    resume_call = true;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&barrier);
    CHECK(pthread_join(thread,NULL) == 0);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
}

static const uint8_t loop[] = {0x48,0x89,0xf8,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
static void trace_call(struct rc_runtime *r, rc_handle h)
{
    uint64_t value = 19;
    CHECK(rc_runtime_call(r,h,100,&value,NULL) == RC_OK && value == 0);
}
static struct rc_runtime *trace_runtime(rc_handle *h)
{
    struct rc_runtime_config c = rc_runtime_default_config();
    c.policy = RC_POLICY_TRACE; c.trace_hot_threshold = 2; c.retry_calls = 2;
    c.executable_budget = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime *r = NULL;
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    CHECK(rc_runtime_register(r,loop,sizeof(loop),h,NULL) == RC_OK);
    return r;
}
static void test_trace_failures(void)
{
    rc_handle h;
    struct rc_runtime *r = trace_runtime(&h);
    malloc_calls = 0; trace_call(r,h);
    size_t allocations = malloc_calls;
    CHECK(function_stats(r,h).compilations == 1);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_allocations == 0);
    for (size_t i = 1; i <= allocations; ++i) {
        r = trace_runtime(&h);
        malloc_calls = 0; fail_at = i;
        trace_call(r,h);
        fail_at = 0;
        struct rc_function_stats s = function_stats(r,h);
        /* Optional recording compaction may fail without losing usable IR. */
        CHECK((s.trace_aborts + s.compilation_failures == 1 && s.trace_entries == 0) ||
              (s.compilations == 1 && s.trace_entries > 0));
        accounted(r);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_allocations == 0 && live_maps == 0);
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        r = trace_runtime(&h);
        fail_map = mode == 0; fail_protect = mode != 0; fail_release = mode == 2;
        trace_call(r,h);
        CHECK(function_stats(r,h).compilation_failures == 1 && function_stats(r,h).trace_entries == 0);
        CHECK(live_maps == (mode == 2 ? 1U : 0U));
        accounted(r);
        if (mode == 2) {
            CHECK(rc_runtime_remove(r,h,NULL) == RC_SYSTEM);
            CHECK(rc_runtime_destroy(&r,NULL) == RC_SYSTEM && r != NULL);
        }
        fail_map = false; fail_protect = false; fail_release = false;
        CHECK(rc_runtime_clear(r,NULL) == RC_OK);
        for (unsigned j = 0; j < 4; ++j) trace_call(r,h);
        CHECK(function_stats(r,h).compilations == 1);
        fail_release = true;
        CHECK(rc_runtime_invalidate(r,h,NULL) == RC_SYSTEM);
        uint64_t entries = function_stats(r,h).trace_entries;
        trace_call(r,h);
        CHECK(function_stats(r,h).trace_entries == entries);
        accounted(r);
        fail_release = false;
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_allocations == 0 && live_maps == 0);
    }
}

static void test_trace_registration(void)
{
    struct rc_runtime *r = create(RC_POLICY_TRACE);
    rc_handle h;
    malloc_calls = 0;
    CHECK(rc_runtime_register(r,loop,sizeof(loop),&h,NULL) == RC_OK);
    size_t allocations = malloc_calls;
    CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    size_t baseline = live_allocations;
    for (size_t i = 1; i <= allocations; ++i) {
        malloc_calls = 0; fail_at = i; h = 99;
        CHECK(rc_runtime_register(r,loop,sizeof(loop),&h,NULL) == RC_MEMORY && h == 99);
        CHECK(live_allocations == baseline);
    }
    fail_at = 0;
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_allocations == 0);
    bool retained = false;
    for (size_t i = 1; i < 16 && !retained; ++i) {
        r = trace_runtime(&h);
        malloc_calls = 0; fail_at = i; fail_release = true;
        trace_call(r,h);
        fail_at = 0;
        if (function_stats(r,h).compilation_failures != 0 && live_maps != 0) {
            retained = true;
            CHECK(function_stats(r,h).trace_entries == 0);
            CHECK(function_stats(r,h).last_compile_error.system_errno == EINVAL);
            accounted(r);
        }
        fail_release = false;
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_maps == 0 && live_allocations == 0);
    }
    CHECK(retained);
}

static void *trace_pinned_worker(void *context)
{
    struct running *run = context;
    block_clock = true; skip_clocks = 1;
    trace_call(run->runtime,run->handle);
    return NULL;
}
static void test_trace_pins(void)
{
    rc_handle h;
    struct rc_runtime *r = trace_runtime(&h);
    trace_call(r,h);
    CHECK(function_stats(r,h).compilations == 1);
    blocked = false; resume_call = false;
    struct running run = {r,h,true};
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,trace_pinned_worker,&run) == 0);
    pthread_mutex_lock(&barrier);
    while (!blocked) pthread_cond_wait(&condition,&barrier);
    pthread_mutex_unlock(&barrier);
    CHECK(rc_runtime_remove(r,h,NULL) == RC_BUSY);
    CHECK(rc_runtime_invalidate(r,h,NULL) == RC_BUSY);
    CHECK(rc_runtime_clear(r,NULL) == RC_BUSY);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_BUSY && r != NULL);
    uint8_t other[sizeof(loop)+1];
    memcpy(other,loop,sizeof(loop)-1);
    other[sizeof(loop)-1] = 0x90; other[sizeof(loop)] = 0xc3;
    rc_handle b;
    CHECK(rc_runtime_register(r,other,sizeof(other),&b,NULL) == RC_OK);
    trace_call(r,b);
    CHECK(function_stats(r,b).last_compile_error.status == RC_CAPACITY);
    CHECK(live_maps == 1); accounted(r);
    pthread_mutex_lock(&barrier);
    resume_call = true; pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&barrier);
    CHECK(pthread_join(thread,NULL) == 0);
    CHECK(function_stats(r,h).trace_entries == 2);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && live_allocations == 0 && live_maps == 0);
}

static enum rc_status integer_build(struct rc_integer_program **p)
{
    struct rc_builder *b = NULL;
    enum rc_status status = rc_builder_create(2,2,&b,NULL);
    const struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    for (size_t n = 0; status == RC_OK && n < 4; ++n)
        status = rc_builder_append(b,insns[n],NULL);
    if (status == RC_OK) status = rc_builder_finalize(b,p,NULL);
    rc_builder_destroy(b);
    return status;
}
static void integer_call(struct rc_runtime *r, rc_handle h)
{
    uint64_t args[] = {UINT64_MAX,1}, value = 19;
    CHECK(rc_runtime_call_integer(r,h,args,2,&value,NULL) == RC_OK && value == 0);
}
static void test_integer_failures(void)
{
    struct rc_integer_program *p = NULL;
    malloc_calls = 0;
    CHECK(integer_build(&p) == RC_OK);
    size_t allocations = malloc_calls;
    rc_integer_program_destroy(p); p = NULL;
    for (size_t n = 1; n <= allocations; ++n) {
        malloc_calls = 0; fail_at = n;
        CHECK(integer_build(&p) == RC_MEMORY && p == NULL);
        fail_at = 0;
        CHECK(live_allocations == 0);
    }
    struct rc_builder *builder = NULL;
    CHECK(rc_builder_create(1,0,&builder,NULL) == RC_OK);
    for (unsigned n = 0; n < 16; ++n)
        CHECK(rc_builder_append(builder,(struct rc_integer_insn){.op=RC_INT_CONSTANT,.immediate=n},NULL) == RC_OK);
    malloc_calls = 0; fail_at = 1;
    CHECK(rc_builder_append(builder,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL) == RC_MEMORY);
    fail_at = 0;
    CHECK(rc_builder_append(builder,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL) == RC_OK);
    CHECK(rc_builder_finalize(builder,&p,NULL) == RC_OK);
    uint64_t value;
    CHECK(rc_integer_interpret(p,NULL,0,0,&value,NULL) == RC_OK && value == 15);
    rc_integer_program_destroy(p); p = NULL; rc_builder_destroy(builder);
    CHECK(integer_build(&p) == RC_OK);
    struct rc_runtime *r = create(RC_POLICY_FIXED);
    rc_handle h;
    malloc_calls = 0;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    allocations = malloc_calls;
    CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    for (size_t n = 1; n <= allocations; ++n) {
        malloc_calls = 0; fail_at = n; h = 91;
        CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_MEMORY && h == 91);
        fail_at = 0;
    }
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    for (unsigned mode = 0; mode < 3; ++mode) {
        r = create(RC_POLICY_FIXED);
        CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
        integer_call(r,h);
        fail_map = mode == 0; fail_protect = mode != 0; fail_release = mode == 2;
        integer_call(r,h);
        CHECK(function_stats(r,h).compilation_failures == 1 && function_stats(r,h).jitted == 0);
        accounted(r);
        if (mode == 2) CHECK(rc_runtime_destroy(&r,NULL) == RC_SYSTEM && r != NULL);
        fail_map = false; fail_protect = false; fail_release = false;
        for (unsigned n = 0; n < 32; ++n) integer_call(r,h);
        CHECK(function_stats(r,h).jitted > 0);
        fail_release = true;
        CHECK(rc_runtime_invalidate(r,h,NULL) == RC_SYSTEM);
        uint64_t jitted = function_stats(r,h).jitted;
        integer_call(r,h);
        CHECK(function_stats(r,h).jitted == jitted);
        accounted(r);
        fail_release = false;
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    }
    rc_integer_program_destroy(p);
    CHECK(live_allocations == 0 && live_maps == 0);
}

static void test_memory_failures(void)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    CHECK(rc_builder_create(1,1,&b,NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_ARGUMENT},NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_LOAD_U64},NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK); rc_builder_destroy(b);
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct rc_runtime *r = create(RC_POLICY_FIXED); rc_handle h;
        CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
        uint8_t data[] = {1,2,3,4,5,6,7,8}; struct rc_memory_view view = {data,8};
        uint64_t arg = 0, value;
        CHECK(rc_runtime_call_integer_view(r,h,&arg,1,&view,&value,NULL) == RC_OK);
        fail_map = mode == 0; fail_protect = mode != 0; fail_release = mode == 2;
        CHECK(rc_runtime_call_integer_view(r,h,&arg,1,&view,&value,NULL) == RC_OK && value == UINT64_C(0x0807060504030201));
        CHECK(function_stats(r,h).compilation_failures == 1 && function_stats(r,h).jitted == 0);
        arg = UINT64_MAX; value = 91; struct rc_error error;
        CHECK(rc_runtime_call_integer_view(r,h,&arg,1,&view,&value,&error) == RC_BOUNDS && value == 91 && error.offset == 1);
        accounted(r);
        if (mode == 2) CHECK(rc_runtime_destroy(&r,NULL) == RC_SYSTEM && r != NULL);
        fail_map = false; fail_protect = false; fail_release = false;
        arg = 0;
        for (unsigned n = 0; n < 32; ++n)
            CHECK(rc_runtime_call_integer_view(r,h,&arg,1,&view,&value,NULL) == RC_OK);
        CHECK(function_stats(r,h).jitted > 0);
        fail_release = true; CHECK(rc_runtime_invalidate(r,h,NULL) == RC_SYSTEM);
        CHECK(rc_runtime_call_integer_view(r,h,&arg,1,&view,&value,NULL) == RC_OK);
        accounted(r); fail_release = false;
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    }
    rc_integer_program_destroy(p); CHECK(live_maps == 0 && live_allocations == 0);
}

int main(void)
{
    test_memory_failures(); test_integer_failures(); test_trace_registration(); test_trace_failures(); test_trace_pins(); test_allocations(); test_system_failures(); test_retry_isolation(); test_policy(); test_pins();
    CHECK(live_maps == 0 && live_bytes == 0 && live_allocations == 0);
    puts("runtime allocation, mapping, protection, release, policy timing and execution pin failures passed");
    return 0;
}
