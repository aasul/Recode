#define _POSIX_C_SOURCE 200809L
#include "internal.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct translation {
    struct translation *next;
    struct rc_program program;
    struct rc_code code;
    size_t references, pins, generated;
    uint64_t retry_after;
};

struct function {
    struct function *next;
    struct translation *translation;
    rc_handle handle;
    size_t active;
    struct rc_function_stats stats;
    uint64_t interpreted_cost, native_cost;
    unsigned slow_native_samples;
    uint64_t window_start, window_calls;
};

struct rc_runtime {
    pthread_mutex_t mutex;
    struct rc_runtime_config config;
    struct rc_runtime_stats stats;
    struct function *functions;
    struct translation *translations; /* MRU first, including entries without code. */
    rc_handle next_handle;
    size_t page_size;
    uint64_t compile_cost;
};

static uint64_t now_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0 || t.tv_sec < 0)
        return 0;
    return rc_saturating_add((uint64_t)t.tv_sec > UINT64_MAX / UINT64_C(1000000000)
        ? UINT64_MAX : (uint64_t)t.tv_sec * UINT64_C(1000000000), (uint64_t)t.tv_nsec);
}

static void add(uint64_t *value, uint64_t amount)
{
    *value = rc_saturating_add(*value, amount);
}

static uint64_t elapsed(uint64_t start, uint64_t end)
{
    return start != 0 && end >= start ? end - start : 0;
}

/* Bound scheduling outliers for policy estimates, retaining raw sampled totals. */
static uint64_t average(uint64_t old, uint64_t sample)
{
    if (sample > UINT64_C(1000000))
        sample = UINT64_C(1000000);
    return old == 0 ? sample : old - old / 8 + sample / 8;
}

struct rc_runtime_config rc_runtime_default_config(void)
{
    return (struct rc_runtime_config){
        .executable_budget = 4 * 1024 * 1024, .policy = RC_POLICY_ADAPTIVE,
        .optimize = true, .hot_threshold = 32, .retry_calls = 128,
        .sample_period = 16, .expected_reuse = 4096,
        .horizon_ns = UINT64_C(100000000), .initial_compile_ns = 50000,
        .initial_native_ns = 50
    };
}

enum rc_status rc_runtime_create(const struct rc_runtime_config *config,
                                  struct rc_runtime **out, struct rc_error *error)
{
    if (config == NULL || out == NULL || *out != NULL ||
        config->policy < RC_POLICY_INTERPRET || config->policy > RC_POLICY_ADAPTIVE ||
        config->hot_threshold == 0 || config->retry_calls == 0 ||
        config->sample_period == 0 || config->expected_reuse == 0 ||
        config->horizon_ns == 0 || config->initial_compile_ns == 0)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return rc_fail(error, RC_SYSTEM, 0, errno);
    struct rc_runtime *r = malloc(sizeof(*r));
    if (r == NULL)
        return rc_fail(error, RC_MEMORY, 0, 0);
    *r = (struct rc_runtime){.config = *config, .next_handle = 1,
        .page_size = (size_t)page, .compile_cost = config->initial_compile_ns};
    int status = pthread_mutex_init(&r->mutex, NULL);
    if (status != 0) {
        free(r);
        return rc_fail(error, RC_SYSTEM, 0, status);
    }
    r->stats.metadata_bytes = sizeof(*r);
    *out = r;
    return rc_fail(error, RC_OK, 0, 0);
}

static struct function *find_function(struct rc_runtime *r, rc_handle handle)
{
    for (struct function *f = r->functions; f != NULL; f = f->next)
        if (f->handle == handle)
            return f;
    return NULL;
}

static bool same_program(const struct rc_program *a, const struct rc_program *b)
{
    if (a->count != b->count)
        return false;
    for (size_t i = 0; i < a->count; ++i)
        if (a->insns[i].op != b->insns[i].op ||
            a->insns[i].immediate != b->insns[i].immediate)
            return false;
    return true;
}

enum rc_status rc_runtime_register(struct rc_runtime *r, const uint8_t *bytes,
                                    size_t length, rc_handle *out, struct rc_error *error)
{
    if (r == NULL || out == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    struct rc_program decoded = {0};
    enum rc_status status = rc_decode(bytes, length, &decoded, error);
    if (status != RC_OK)
        return status;
    pthread_mutex_lock(&r->mutex);
    struct translation *t = r->translations;
    while (t != NULL && !same_program(&t->program, &decoded))
        t = t->next;
    size_t program_bytes = decoded.count * sizeof(*decoded.insns);
    size_t extra = sizeof(struct function) + (t == NULL ? sizeof(*t) : 0);
    if (r->next_handle == 0 || r->stats.metadata_bytes > SIZE_MAX - extra ||
        (t == NULL && r->stats.program_bytes > SIZE_MAX - program_bytes) ||
        (t != NULL && t->references == SIZE_MAX)) {
        status = RC_CAPACITY;
        goto done;
    }
    struct function *f = malloc(sizeof(*f));
    if (f == NULL) {
        status = RC_MEMORY;
        goto done;
    }
    if (t == NULL) {
        t = malloc(sizeof(*t));
        struct rc_insn *insns = malloc(program_bytes);
        if (t == NULL || insns == NULL) {
            free(t);
            free(insns);
            free(f);
            status = RC_MEMORY;
            goto done;
        }
        memcpy(insns, decoded.insns, program_bytes);
        *t = (struct translation){.program = {insns, decoded.count}, .next = r->translations};
        r->translations = t;
        ++r->stats.translations;
        r->stats.program_bytes += program_bytes;
    }
    *f = (struct function){.next = r->functions, .translation = t,
        .handle = r->next_handle};
    /* Unsigned wrap to zero exhausts the handle space permanently. */
    ++r->next_handle;
    ++t->references;
    r->functions = f;
    ++r->stats.functions;
    r->stats.metadata_bytes += extra;
    *out = f->handle;
done:
    pthread_mutex_unlock(&r->mutex);
    rc_program_free(&decoded);
    return rc_fail(error, status, 0, 0);
}

static void touch(struct rc_runtime *r, struct translation *t)
{
    struct translation **link = &r->translations;
    while (*link != t)
        link = &(*link)->next;
    *link = t->next;
    t->next = r->translations;
    r->translations = t;
}

static enum rc_status release_mapping(struct rc_runtime *r, struct translation *t,
                                       struct rc_error *error)
{
    if (t->pins != 0)
        return rc_fail(error, RC_BUSY, 0, 0);
    size_t size = t->code.size;
    /* A failed invalidation must not publish the old function again. */
    t->code.function = NULL;
    struct rc_error release_error;
    enum rc_status status = rc_code_free(&t->code, &release_error);
    if (status != RC_OK) {
        r->stats.last_cache_error = release_error;
        if (error != NULL)
            *error = release_error;
        add(&r->stats.release_failures, 1);
        return status;
    }
    r->stats.executable_bytes -= size;
    return RC_OK;
}

static enum rc_status make_room(struct rc_runtime *r, size_t needed, struct rc_error *error)
{
    if (needed > r->config.executable_budget)
        return rc_fail(error, RC_CAPACITY, 0, 0);
    while (needed > r->config.executable_budget - r->stats.executable_bytes) {
        struct translation *victim = NULL;
        for (struct translation *t = r->translations; t != NULL; t = t->next)
            if (t->code.mapping != NULL && t->pins == 0)
                victim = t;
        if (victim == NULL)
            return rc_fail(error, RC_CAPACITY, 0, 0);
        enum rc_status status = release_mapping(r, victim, error);
        if (status != RC_OK)
            return status;
        victim->retry_after = rc_saturating_add(r->stats.invocations, r->config.retry_calls);
        add(&r->stats.evictions, 1);
    }
    return RC_OK;
}

static bool should_compile(struct rc_runtime *r, struct function *f, uint64_t now)
{
    struct rc_function_stats *s = &f->stats;
    if (r->config.policy == RC_POLICY_INTERPRET || s->interpreted == 0 ||
        s->invocations < r->config.hot_threshold ||
        r->stats.invocations <= f->translation->retry_after)
        return false;
    if (r->config.policy == RC_POLICY_FIXED)
        return true;
    bool room = r->config.executable_budget >= r->page_size &&
        r->config.executable_budget - r->stats.executable_bytes >= r->page_size;
    for (struct translation *t = r->translations; !room && t != NULL; t = t->next)
        room = t->code.mapping != NULL && t->pins == 0 && r->config.executable_budget >= r->page_size;
    bool worthwhile = false;
    uint64_t native = f->native_cost != 0 ? f->native_cost : r->config.initial_native_ns;
    uint64_t window = elapsed(f->window_start, now);
    if (room && s->interpreted_samples >= 8 && f->interpreted_cost > native && window != 0) {
        uint64_t saving = f->interpreted_cost - native;
        uint64_t reuse = f->window_calls > UINT64_MAX / r->config.horizon_ns
            ? UINT64_MAX : f->window_calls * r->config.horizon_ns / window;
        if (reuse > r->config.expected_reuse)
            reuse = r->config.expected_reuse;
        /* Require twice the predicted break-even reuse to absorb timer noise. */
        uint64_t payback = r->compile_cost / saving;
        worthwhile = payback < reuse / 2;
    }
    if (!worthwhile)
        add(&s->policy_postponed, 1);
    return worthwhile;
}

static void compile_translation(struct rc_runtime *r, struct function *f)
{
    struct translation *t = f->translation;
    struct rc_error error = {0};
    struct rc_program prepared = {0};
    uint64_t start = now_ns();
    add(&f->stats.compilation_attempts, 1);
    enum rc_status status = RC_OK;
    if (t->code.mapping != NULL)
        status = release_mapping(r, t, &error);
    if (status == RC_OK) {
        prepared.count = t->program.count;
        prepared.insns = malloc(prepared.count * sizeof(*prepared.insns));
        if (prepared.insns == NULL)
            status = rc_fail(&error, RC_MEMORY, 0, 0);
        else
            memcpy(prepared.insns, t->program.insns, prepared.count * sizeof(*prepared.insns));
    }
    if (status == RC_OK && r->config.optimize)
        status = rc_optimize(&prepared, &error);
    size_t emitted = 0, mapped = 0;
    if (status == RC_OK)
        status = rc_emit(&prepared, NULL, 0, &emitted, &error);
    if (status == RC_OK) {
        if (emitted > SIZE_MAX - (r->page_size - 1))
            status = rc_fail(&error, RC_MEMORY, 0, 0);
        else {
            mapped = (emitted + r->page_size - 1) / r->page_size * r->page_size;
            status = make_room(r, mapped, &error);
        }
    }
    if (status == RC_OK) {
        status = rc_map_program(&prepared, mapped, &t->code, &error);
        /* Includes mappings retained when both generation and rollback fail. */
        if (status != RC_OK && t->code.mapping != NULL) {
            add(&r->stats.release_failures, 1);
            r->stats.last_cache_error = error;
        }
        r->stats.executable_bytes += t->code.size;
        if (r->stats.executable_bytes > r->stats.peak_executable_bytes)
            r->stats.peak_executable_bytes = r->stats.executable_bytes;
    }
    rc_program_free(&prepared);
    uint64_t cost = elapsed(start, now_ns());
    add(&f->stats.compilation_ns, cost);
    if (cost == 0)
        add(&f->stats.timing_failures, 1);
    if (status == RC_OK) {
        t->generated = emitted;
        f->stats.generated_size = emitted;
        f->stats.last_compile_error = (struct rc_error){0};
        add(&f->stats.compilations, 1);
        add(&r->stats.compilations, 1);
        /* Compilation outliers matter to break-even, so do not clip them. */
        if (cost != 0)
            r->compile_cost = r->compile_cost - r->compile_cost / 8 + cost / 8;
        touch(r, t);
    } else {
        f->stats.last_compile_error = error;
        add(&f->stats.compilation_failures, 1);
        add(&r->stats.compilation_failures, 1);
        if (status == RC_CAPACITY)
            add(&r->stats.capacity_failures, 1);
        uint64_t failures = f->stats.compilation_failures;
        uint64_t delay = r->config.retry_calls;
        for (uint64_t i = 1; i < failures && i < 10; ++i)
            delay = rc_saturating_add(delay, delay);
        t->retry_after = rc_saturating_add(r->stats.invocations, delay);
    }
}

enum rc_status rc_runtime_call(struct rc_runtime *r, rc_handle handle,
                                uint64_t argument, uint64_t *result, struct rc_error *error)
{
    if (r == NULL || result == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    struct function *f = find_function(r, handle);
    if (f == NULL) {
        pthread_mutex_unlock(&r->mutex);
        return rc_fail(error, RC_NOT_FOUND, 0, 0);
    }
    struct translation *t = f->translation;
    if (f->active == SIZE_MAX || t->pins == SIZE_MAX || r->stats.active_calls == SIZE_MAX) {
        pthread_mutex_unlock(&r->mutex);
        return rc_fail(error, RC_CAPACITY, 0, 0);
    }
    ++f->active;
    ++r->stats.active_calls;
    add(&f->stats.invocations, 1);
    add(&r->stats.invocations, 1);
    uint64_t tick = 0;
    /* Every handle starts in the interpreter, even if an alias has cached code. */
    if (f->stats.interpreted != 0 && t->code.function != NULL) {
        add(&f->stats.cache_hits, 1);
        add(&r->stats.cache_hits, 1);
    } else {
        add(&f->stats.cache_misses, 1);
        add(&r->stats.cache_misses, 1);
        tick = now_ns();
        if (t->code.function == NULL && should_compile(r, f, tick))
            compile_translation(r, f);
    }
    rc_function native = f->stats.interpreted != 0 ? t->code.function : NULL;
    bool sample = (native == NULL ? f->stats.interpreted_samples : f->stats.compiled_samples) < 8 ||
        f->stats.invocations % r->config.sample_period == 0;
    if (native != NULL) {
        ++t->pins;
        touch(r, t);
        f->stats.generated_size = t->generated;
    }
    if (tick != 0) {
        if (f->window_start == 0 || tick < f->window_start)
            f->window_start = tick;
        if (tick - f->window_start > r->config.horizon_ns) {
            f->window_start = tick;
            f->window_calls = 0;
        }
    }
    add(&f->window_calls, 1);
    pthread_mutex_unlock(&r->mutex);

    uint64_t start = sample ? now_ns() : 0;
    uint64_t value;
    if (native != NULL)
        value = native(argument);
    else
        (void)rc_interpret_validated(&t->program, argument, 0, &value);
    uint64_t duration = sample ? elapsed(start, now_ns()) : 0;

    pthread_mutex_lock(&r->mutex);
    if (native != NULL) {
        --t->pins;
        add(&f->stats.jitted, 1);
        if (sample) {
            add(&f->stats.compiled_samples, 1);
            add(&f->stats.compiled_ns, duration);
            f->native_cost = average(f->native_cost, duration);
            uint64_t margin = f->interpreted_cost / 4;
            if (duration > rc_saturating_add(f->interpreted_cost, margin)) {
                if (f->slow_native_samples < 8)
                    ++f->slow_native_samples;
            } else {
                f->slow_native_samples = 0;
            }
        }
    } else {
        add(&f->stats.interpreted, 1);
        if (sample) {
            add(&f->stats.interpreted_samples, 1);
            add(&f->stats.interpreted_ns, duration);
            f->interpreted_cost = average(f->interpreted_cost, duration);
        }
    }
    if (sample && duration == 0)
        add(&f->stats.timing_failures, 1);
    /* Recheck profitability against measured native calls. Retry only after a
     * cooldown; other aliases may still have different input-dependent costs. */
    if (r->config.policy == RC_POLICY_ADAPTIVE && native != NULL &&
        f->slow_native_samples >= 8 && f->native_cost >
        rc_saturating_add(f->interpreted_cost, f->interpreted_cost / 4) && t->pins == 0) {
        (void)release_mapping(r, t, NULL);
        t->retry_after = rc_saturating_add(r->stats.invocations, r->config.retry_calls);
        add(&f->stats.policy_postponed, 1);
        f->slow_native_samples = 0;
    }
    --f->active;
    --r->stats.active_calls;
    pthread_mutex_unlock(&r->mutex);
    *result = value;
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_runtime_function_stats(struct rc_runtime *r, rc_handle handle,
                                          struct rc_function_stats *out, struct rc_error *error)
{
    if (r == NULL || out == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    struct function *f = find_function(r, handle);
    if (f != NULL)
        *out = f->stats;
    enum rc_status status = f == NULL ? RC_NOT_FOUND : RC_OK;
    pthread_mutex_unlock(&r->mutex);
    return rc_fail(error, status, 0, 0);
}

enum rc_status rc_runtime_get_stats(struct rc_runtime *r,
                                     struct rc_runtime_stats *out, struct rc_error *error)
{
    if (r == NULL || out == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    *out = r->stats;
    pthread_mutex_unlock(&r->mutex);
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_runtime_invalidate(struct rc_runtime *r, rc_handle handle,
                                      struct rc_error *error)
{
    if (r == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    struct function *f = find_function(r, handle);
    enum rc_status status = f == NULL ? rc_fail(error, RC_NOT_FOUND, 0, 0)
        : release_mapping(r, f->translation, error);
    if (f != NULL && status == RC_OK)
        f->translation->retry_after = rc_saturating_add(r->stats.invocations, r->config.retry_calls);
    pthread_mutex_unlock(&r->mutex);
    return status == RC_OK ? rc_fail(error, RC_OK, 0, 0) : status;
}

static enum rc_status clear_locked(struct rc_runtime *r, struct rc_error *error)
{
    enum rc_status status = RC_OK;
    for (struct translation *t = r->translations; t != NULL; t = t->next) {
        struct rc_error current = {0};
        enum rc_status released = release_mapping(r, t, &current);
        if (released != RC_OK && (status == RC_OK || status == RC_BUSY)) {
            status = released;
            if (error != NULL)
                *error = current;
        }
    }
    return status;
}

enum rc_status rc_runtime_clear(struct rc_runtime *r, struct rc_error *error)
{
    if (r == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    enum rc_status status = clear_locked(r, error);
    pthread_mutex_unlock(&r->mutex);
    return status == RC_OK ? rc_fail(error, RC_OK, 0, 0) : status;
}

enum rc_status rc_runtime_remove(struct rc_runtime *r, rc_handle handle,
                                  struct rc_error *error)
{
    if (r == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    struct function **link = &r->functions;
    while (*link != NULL && (*link)->handle != handle)
        link = &(*link)->next;
    struct function *f = *link;
    enum rc_status status = RC_OK;
    if (f == NULL)
        status = rc_fail(error, RC_NOT_FOUND, 0, 0);
    else if (f->active != 0)
        status = rc_fail(error, RC_BUSY, 0, 0);
    else if (f->translation->references == 1)
        status = release_mapping(r, f->translation, error);
    if (status == RC_OK) {
        struct translation *t = f->translation;
        if (--t->references == 0) {
            struct translation **entry = &r->translations;
            while (*entry != t)
                entry = &(*entry)->next;
            *entry = t->next;
            r->stats.program_bytes -= t->program.count * sizeof(*t->program.insns);
            r->stats.metadata_bytes -= sizeof(*t);
            --r->stats.translations;
            rc_program_free(&t->program);
            free(t);
        }
        *link = f->next;
        free(f);
        --r->stats.functions;
        r->stats.metadata_bytes -= sizeof(*f);
    }
    pthread_mutex_unlock(&r->mutex);
    return status == RC_OK ? rc_fail(error, RC_OK, 0, 0) : status;
}

enum rc_status rc_runtime_destroy(struct rc_runtime **runtime, struct rc_error *error)
{
    if (runtime == NULL || *runtime == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    struct rc_runtime *r = *runtime;
    pthread_mutex_lock(&r->mutex);
    if (r->stats.active_calls != 0) {
        pthread_mutex_unlock(&r->mutex);
        return rc_fail(error, RC_BUSY, 0, 0);
    }
    enum rc_status status = clear_locked(r, error);
    if (status != RC_OK) {
        pthread_mutex_unlock(&r->mutex);
        return status;
    }
    while (r->functions != NULL) {
        struct function *f = r->functions;
        r->functions = f->next;
        free(f);
    }
    while (r->translations != NULL) {
        struct translation *t = r->translations;
        r->translations = t->next;
        rc_program_free(&t->program);
        free(t);
    }
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_destroy(&r->mutex);
    free(r);
    *runtime = NULL;
    return rc_fail(error, RC_OK, 0, 0);
}
