#define _POSIX_C_SOURCE 200809L
#include "trace.h"
#include "integer.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct trace_slot {
    struct rc_trace trace;
    struct rc_code code;
    rc_trace_function function;
    uint64_t edges, retry_after;
    unsigned failures, short_exits;
    bool recording, disabled;
};

struct translation {
    struct translation *next, *previous, *hash_next;
    uint64_t hash;
    struct rc_program program;
    struct rc_integer_program *integer;
    rc_integer_function integer_function;
    struct rc_code code;
    size_t references, pins, generated;
    uint64_t invocations, retry_after;
    struct trace_slot *traces;
    size_t trace_count;
};

struct function {
    struct function *next, *previous, *hash_next;
    struct translation *translation;
    rc_handle handle;
    size_t active;
    struct rc_function_stats stats;
    uint64_t interpreted_cost, native_cost;
    unsigned slow_native_samples;
    uint64_t window_start, window_calls;
};

struct lookup_bucket { struct function *function; struct translation *translation; };

struct rc_runtime {
    pthread_mutex_t mutex;
    struct rc_runtime_config config;
    struct rc_runtime_stats stats;
    struct function *functions;
    struct translation *translations, *oldest; /* MRU first, including entries without code. */
    struct lookup_bucket *buckets;
    size_t bucket_count;
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
        .initial_native_ns = 50, .trace_hot_threshold = 32,
        .trace_max_instructions = 256, .trace_max_candidates = 4
    };
}

enum rc_status rc_runtime_create(const struct rc_runtime_config *config,
                                  struct rc_runtime **out, struct rc_error *error)
{
    if (config == NULL || out == NULL || *out != NULL ||
        config->policy < RC_POLICY_INTERPRET || config->policy > RC_POLICY_TRACE ||
        config->hot_threshold == 0 || config->retry_calls == 0 ||
        config->sample_period == 0 || config->expected_reuse == 0 ||
        config->horizon_ns == 0 || config->initial_compile_ns == 0 ||
        (config->policy == RC_POLICY_TRACE &&
         (config->trace_hot_threshold == 0 || config->trace_max_instructions == 0 ||
          config->trace_max_instructions > RC_TRACE_MAX_OPS || config->trace_max_candidates == 0 ||
          config->trace_max_candidates > RC_TRACE_MAX_CANDIDATES)))
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
    if (r->bucket_count == 0) return NULL;
    for (struct function *f = r->buckets[handle % r->bucket_count].function;
         f != NULL; f = f->hash_next)
        if (f->handle == handle) return f;
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

static uint64_t program_hash(const struct rc_program *p)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < p->count; ++i) {
        hash = (hash ^ (uint64_t)p->insns[i].op) * UINT64_C(1099511628211);
        hash = (hash ^ p->insns[i].immediate) * UINT64_C(1099511628211);
    }
    return hash;
}

static enum rc_status grow_index(struct rc_runtime *r)
{
    if (r->stats.functions < r->bucket_count) return RC_OK;
    size_t count = r->bucket_count == 0 ? 16 : r->bucket_count * 2;
    if (count < r->bucket_count || count > SIZE_MAX / sizeof(*r->buckets) ||
        count * sizeof(*r->buckets) > SIZE_MAX - r->stats.metadata_bytes)
        return RC_CAPACITY;
    struct lookup_bucket *buckets = calloc(count,sizeof(*buckets));
    if (buckets == NULL) return RC_MEMORY;
    for (struct function *f = r->functions; f != NULL; f = f->next) {
        size_t i = f->handle % count;
        f->hash_next = buckets[i].function;
        buckets[i].function = f;
    }
    for (struct translation *t = r->translations; t != NULL; t = t->next) {
        size_t i = t->hash % count;
        t->hash_next = buckets[i].translation;
        buckets[i].translation = t;
    }
    r->stats.metadata_bytes += (count - r->bucket_count) * sizeof(*buckets);
    free(r->buckets);
    r->buckets = buckets;
    r->bucket_count = count;
    return RC_OK;
}

static void unlink_translation(struct rc_runtime *r, struct translation *t)
{
    if (t->previous != NULL) t->previous->next = t->next;
    else r->translations = t->next;
    if (t->next != NULL) t->next->previous = t->previous;
    else r->oldest = t->previous;
}

static size_t trace_headers(const struct rc_program *program, size_t limit, uint32_t *headers)
{
    size_t count = 0;
    for (size_t i = 0; i < program->count && count < limit; ++i) {
        struct rc_insn insn = program->insns[i];
        if (!rc_is_branch(insn.op) || insn.immediate > i) continue;
        bool found = false;
        for (size_t j = 0; j < count; ++j) found |= headers[j] == insn.immediate;
        if (!found) headers[count++] = insn.immediate;
    }
    return count;
}

static bool same_integer(const struct rc_integer_program *a, const struct rc_integer_program *b)
{
    if (a->registers != b->registers || a->arguments != b->arguments || a->count != b->count)
        return false;
    for (size_t n = 0; n < a->count; ++n) {
        struct rc_integer_insn x = a->insns[n], y = b->insns[n];
        if (x.op != y.op || x.dst != y.dst || x.left != y.left || x.right != y.right ||
            x.target != y.target || x.immediate != y.immediate) return false;
    }
    return true;
}

static uint64_t integer_hash(const struct rc_integer_program *p)
{
    uint64_t hash = (UINT64_C(14695981039346656037) ^ p->registers) * UINT64_C(1099511628211);
    hash = (hash ^ p->arguments) * UINT64_C(1099511628211);
    for (size_t n = 0; n < p->count; ++n) {
        struct rc_integer_insn i = p->insns[n];
        const uint64_t fields[] = {(uint64_t)i.op,i.dst,i.left,i.right,i.target,i.immediate};
        for (size_t f = 0; f < sizeof(fields)/sizeof(fields[0]); ++f)
            hash = (hash ^ fields[f]) * UINT64_C(1099511628211);
    }
    return hash;
}

static enum rc_status register_program(struct rc_runtime *r, struct rc_program decoded,
                                        const struct rc_integer_program *integer,
                                        rc_handle *out, struct rc_error *error)
{
    enum rc_status status;
    pthread_mutex_lock(&r->mutex);
    status = grow_index(r);
    if (status != RC_OK) goto done;
    uint64_t hash = integer != NULL ? integer_hash(integer) : program_hash(&decoded);
    struct translation *t = r->buckets[hash % r->bucket_count].translation;
    while (t != NULL && (t->hash != hash || (integer != NULL ?
           t->integer == NULL || !same_integer(t->integer,integer) :
           t->integer != NULL || !same_program(&t->program,&decoded))))
        t = t->hash_next;
    size_t program_bytes = integer != NULL ? sizeof(*integer) + integer->count * sizeof(*integer->insns)
        : decoded.count * sizeof(*decoded.insns);
    uint32_t headers[RC_TRACE_MAX_CANDIDATES];
    size_t candidates = integer == NULL && t == NULL && r->config.policy == RC_POLICY_TRACE
        ? trace_headers(&decoded,r->config.trace_max_candidates,headers) : 0;
    size_t trace_bytes = candidates * sizeof(struct trace_slot);
    size_t extra = sizeof(struct function) + (t == NULL ? sizeof(*t) + trace_bytes : 0);
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
        struct rc_insn *insns = NULL;
        struct rc_integer_program *copy = NULL;
        status = RC_OK;
        if (t != NULL) {
            if (integer != NULL) status = rc_integer_copy(integer,&copy,error);
            else {
                insns = malloc(program_bytes);
                if (insns == NULL) status = RC_MEMORY;
                else memcpy(insns,decoded.insns,program_bytes);
            }
        }
        if (t == NULL || status != RC_OK) {
            free(t); free(f);
            status = RC_MEMORY;
            goto done;
        }
        *t = (struct translation){.program = {insns, decoded.count}, .integer = copy,
            .next = r->translations};
        if (trace_bytes != 0) {
            t->traces = malloc(trace_bytes);
            if (t->traces == NULL) {
                free(insns); free(t); free(f);
                status = RC_MEMORY;
                goto done;
            }
            memset(t->traces,0,trace_bytes);
            t->trace_count = candidates;
            for (size_t i = 0; i < candidates; ++i) t->traces[i].trace.header = headers[i];
        }
        size_t bucket = hash % r->bucket_count;
        t->hash = hash;
        t->hash_next = r->buckets[bucket].translation;
        r->buckets[bucket].translation = t;
        if (t->next != NULL) t->next->previous = t;
        else r->oldest = t;
        r->translations = t;
        ++r->stats.translations;
        r->stats.program_bytes += program_bytes;
    }
    *f = (struct function){.next = r->functions, .translation = t,
        .handle = r->next_handle};
    /* Unsigned wrap to zero exhausts the handle space permanently. */
    ++r->next_handle;
    ++t->references;
    size_t bucket = f->handle % r->bucket_count;
    f->hash_next = r->buckets[bucket].function;
    r->buckets[bucket].function = f;
    if (f->next != NULL) f->next->previous = f;
    r->functions = f;
    ++r->stats.functions;
    r->stats.metadata_bytes += extra;
    *out = f->handle;
done:
    if (r->stats.functions == 0 && r->buckets != NULL) {
        r->stats.metadata_bytes -= r->bucket_count * sizeof(*r->buckets);
        free(r->buckets);
        r->buckets = NULL;
        r->bucket_count = 0;
    }
    pthread_mutex_unlock(&r->mutex);
    return rc_fail(error, status, 0, 0);
}

enum rc_status rc_runtime_register(struct rc_runtime *r, const uint8_t *bytes,
                                    size_t length, rc_handle *out, struct rc_error *error)
{
    if (r == NULL || out == NULL) return rc_fail(error,RC_ARGUMENT,0,0);
    struct rc_program decoded = {0};
    enum rc_status status = rc_decode(bytes,length,&decoded,error);
    if (status == RC_OK) status = register_program(r,decoded,NULL,out,error);
    rc_program_free(&decoded);
    return status;
}

enum rc_status rc_runtime_register_integer(struct rc_runtime *r,
                                            const struct rc_integer_program *program,
                                            rc_handle *out, struct rc_error *error)
{
    if (r == NULL || program == NULL || out == NULL) return rc_fail(error,RC_ARGUMENT,0,0);
    if (r->config.policy == RC_POLICY_TRACE) return rc_fail(error,RC_UNSUPPORTED,0,0);
    return register_program(r,(struct rc_program){0},program,out,error);
}

static void touch(struct rc_runtime *r, struct translation *t)
{
    unlink_translation(r,t);
    t->previous = NULL;
    t->next = r->translations;
    if (t->next != NULL) t->next->previous = t;
    else r->oldest = t;
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
    t->integer_function = NULL;
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
    enum rc_status result = RC_OK;
    for (size_t i = 0; i < t->trace_count; ++i) {
        struct trace_slot *slot = &t->traces[i];
        size = slot->code.size;
        slot->function = NULL;
        slot->retry_after = rc_saturating_add(t->invocations,r->config.retry_calls);
        status = rc_code_free(&slot->code,&release_error);
        if (status != RC_OK) {
            result = status;
            r->stats.last_cache_error = release_error;
            if (error != NULL) *error = release_error;
            add(&r->stats.release_failures,1);
        } else {
            r->stats.executable_bytes -= size;
        }
    }
    return result;
}

static bool has_mapping(const struct translation *t)
{
    if (t->code.mapping != NULL) return true;
    for (size_t i = 0; i < t->trace_count; ++i)
        if (t->traces[i].code.mapping != NULL) return true;
    return false;
}

static enum rc_status make_room(struct rc_runtime *r, size_t needed, struct rc_error *error)
{
    if (needed > r->config.executable_budget)
        return rc_fail(error, RC_CAPACITY, 0, 0);
    while (needed > r->config.executable_budget - r->stats.executable_bytes) {
        struct translation *victim = NULL;
        for (struct translation *t = r->oldest; t != NULL; t = t->previous)
            if (has_mapping(t) && t->pins == 0) { victim = t; break; }
        if (victim == NULL)
            return rc_fail(error, RC_CAPACITY, 0, 0);
        enum rc_status status = release_mapping(r, victim, error);
        if (status != RC_OK)
            return status;
        victim->retry_after = rc_saturating_add(victim->invocations, r->config.retry_calls);
        add(&r->stats.evictions, 1);
    }
    return RC_OK;
}

static bool should_compile(struct rc_runtime *r, struct function *f, uint64_t now)
{
    struct rc_function_stats *s = &f->stats;
    if ((r->config.policy == RC_POLICY_INTERPRET || r->config.policy == RC_POLICY_TRACE) || s->interpreted == 0 ||
        s->invocations < r->config.hot_threshold ||
        f->translation->invocations <= f->translation->retry_after)
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
    if (status == RC_OK && t->integer == NULL) {
        prepared.count = t->program.count;
        prepared.insns = malloc(prepared.count * sizeof(*prepared.insns));
        if (prepared.insns == NULL)
            status = rc_fail(&error, RC_MEMORY, 0, 0);
        else
            memcpy(prepared.insns, t->program.insns, prepared.count * sizeof(*prepared.insns));
    }
    if (status == RC_OK && t->integer == NULL && r->config.optimize)
        status = rc_optimize(&prepared, &error);
    size_t emitted = 0, mapped = 0;
    if (status == RC_OK)
        status = t->integer != NULL ? rc_integer_emit(t->integer,NULL,0,&emitted,&error)
            : rc_emit(&prepared, NULL, 0, &emitted, &error);
    if (status == RC_OK) {
        if (emitted > SIZE_MAX - (r->page_size - 1))
            status = rc_fail(&error, RC_MEMORY, 0, 0);
        else {
            mapped = (emitted + r->page_size - 1) / r->page_size * r->page_size;
            status = make_room(r, mapped, &error);
        }
    }
    if (status == RC_OK) {
        bool allocated;
        status = t->integer != NULL
            ? rc_integer_map(t->integer,mapped,&t->code,&t->integer_function,&allocated,&error)
            : rc_map_program(&prepared, mapped, &t->code, &allocated, &error);
        size_t peak = r->stats.executable_bytes + (allocated ? mapped : 0);
        if (peak > r->stats.peak_executable_bytes)
            r->stats.peak_executable_bytes = peak;
        /* Includes mappings retained when both generation and rollback fail. */
        if (status != RC_OK && t->code.mapping != NULL) {
            add(&r->stats.release_failures, 1);
            r->stats.last_cache_error = error;
        }
        r->stats.executable_bytes += t->code.size;
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
        t->retry_after = rc_saturating_add(t->invocations, delay);
    }
}

struct trace_call {
    struct rc_runtime *runtime;
    struct function *function;
    struct rc_trace_view views[RC_TRACE_MAX_CANDIDATES];
    bool blocked[RC_TRACE_MAX_CANDIDATES];
    size_t recording;
    uint32_t exits, short_exits;
    uint64_t recording_start;
    struct rc_function_stats stats;
};

/* Compilation and publication share the function compiler's mutex and budget.
 * Existing mappings cannot be replaced while any trace call pins this translation. */
static void compile_trace(struct trace_call *call, struct trace_slot *slot)
{
    struct rc_runtime *r = call->runtime;
    struct translation *t = call->function->translation;
    struct rc_error error = {0};
    size_t emitted = 0, mapped = 0;
    uint64_t start = now_ns();
    add(&call->stats.compilation_attempts,1);
    enum rc_status status = rc_trace_emit(&t->program,&slot->trace,r->config.optimize,
                                          NULL,0,&emitted,&error);
    if (status == RC_OK) {
        mapped = (emitted + r->page_size - 1) / r->page_size * r->page_size;
        status = make_room(r,mapped,&error);
    }
    if (status == RC_OK) {
        bool allocated;
        status = rc_trace_map(&t->program,&slot->trace,r->config.optimize,mapped,
                              &slot->code,&slot->function,&allocated,&error);
        size_t peak = r->stats.executable_bytes + (allocated ? mapped : 0);
        if (peak > r->stats.peak_executable_bytes) r->stats.peak_executable_bytes = peak;
        r->stats.executable_bytes += slot->code.size;
        if (status != RC_OK && slot->code.mapping != NULL) {
            add(&r->stats.release_failures,1);
            r->stats.last_cache_error = error;
        }
    }
    add(&call->stats.compilation_ns,elapsed(start,now_ns()));
    if (status == RC_OK) {
        add(&call->stats.compilations,1);
        add(&r->stats.compilations,1);
        call->stats.generated_size = emitted;
        call->stats.last_compile_error = (struct rc_error){0};
        touch(r,t);
    } else {
        add(&call->stats.compilation_failures,1);
        add(&r->stats.compilation_failures,1);
        if (status == RC_CAPACITY) add(&r->stats.capacity_failures,1);
        call->stats.last_compile_error = error;
        ++slot->failures;
        slot->disabled = slot->failures >= 3;
        slot->retry_after = rc_saturating_add(t->invocations,r->config.retry_calls);
    }
}

static void stop_observing(struct rc_observer *observer)
{
    struct trace_call *call = observer->context;
    for (size_t i = 0; i < call->function->translation->trace_count; ++i)
        if (!call->blocked[i]) return;
    observer->before = NULL;
}

static bool trace_before(struct rc_observer *observer, struct rc_state *state)
{
    struct trace_call *call = observer->context;
    struct translation *t = call->function->translation;
    for (size_t i = 0; i < t->trace_count; ++i) {
        if (call->views[i].function == NULL || call->blocked[i] ||
            call->views[i].header != state->pc) continue;
        state->remaining = RC_TRACE_QUANTUM;
        enum rc_trace_exit reason = call->views[i].function(state);
        uint64_t iterations = RC_TRACE_QUANTUM - state->remaining;
        add(&call->stats.trace_entries,1);
        add(&call->stats.trace_iterations,iterations);
        if (reason == RC_TRACE_SIDE_EXIT) {
            add(&call->stats.trace_side_exits,1);
            call->blocked[i] = true;
            stop_observing(observer);
            call->exits |= UINT32_C(1) << i;
            if (iterations == 0) call->short_exits |= UINT32_C(1) << i;
        } else if (reason == RC_TRACE_YIELD) {
            add(&call->stats.trace_yields,1);
        }
        return reason == RC_TRACE_RETURNED;
    }
    return false;
}

static void trace_after(struct rc_observer *observer, size_t pc,
                         struct rc_state *state, bool returned)
{
    struct trace_call *call = observer->context;
    struct rc_runtime *r = call->runtime;
    struct translation *t = call->function->translation;
    if (observer->recording != NULL) {
        if (observer->recording->stop == RC_TRACE_RECORDING) return;
        pthread_mutex_lock(&r->mutex);
        struct trace_slot *slot = &t->traces[call->recording];
        add(&call->stats.trace_recording_ns,elapsed(call->recording_start,now_ns()));
        add(&call->stats.trace_recorded_ops,slot->trace.count);
        slot->recording = false;
        observer->recording = NULL;
        if (slot->trace.stop == RC_TRACE_LOOP || slot->trace.stop == RC_TRACE_RETURN) {
            size_t used = slot->trace.count * sizeof(*slot->trace.ops);
            struct rc_trace_op *compact = malloc(used);
            if (compact != NULL) {
                memcpy(compact,slot->trace.ops,used);
                free(slot->trace.ops);
                r->stats.metadata_bytes -= (slot->trace.capacity - slot->trace.count) * sizeof(*compact);
                slot->trace.ops = compact;
                slot->trace.capacity = slot->trace.count;
            }
            if (!r->config.trace_record_only) compile_trace(call,slot);
            else slot->disabled = true;
            call->views[call->recording].function = slot->function;
        } else {
            slot->disabled = true;
            add(&call->stats.trace_aborts,1);
            r->stats.metadata_bytes -= slot->trace.capacity * sizeof(*slot->trace.ops);
            free(slot->trace.ops);
            slot->trace.ops = NULL;
        }
        if (slot->disabled) call->blocked[call->recording] = true;
        stop_observing(observer);
        pthread_mutex_unlock(&r->mutex);
        return;
    }
    if (returned || state->pc > pc || !rc_is_branch(t->program.insns[pc].op)) return;
    for (size_t i = 0; i < t->trace_count; ++i) {
        if (call->views[i].header != state->pc || call->blocked[i]) continue;
        if (call->views[i].function != NULL) return;
        pthread_mutex_lock(&r->mutex);
        struct trace_slot *slot = &t->traces[i];
        add(&slot->edges,1);
        if (slot->disabled || slot->recording || t->invocations <= slot->retry_after)
            call->blocked[i] = true;
        bool eligible = !slot->disabled && !slot->recording &&
            t->invocations > slot->retry_after && slot->edges >= r->config.trace_hot_threshold;
        if (eligible && slot->function != NULL) {
            call->views[i].function = slot->function;
            add(&call->stats.trace_hits,1);
        } else if (eligible && slot->code.mapping == NULL) {
            add(&call->stats.trace_misses,1);
            if (slot->trace.ops == NULL) {
                size_t bytes = r->config.trace_max_instructions * sizeof(*slot->trace.ops);
                if (bytes <= SIZE_MAX - r->stats.metadata_bytes)
                    slot->trace.ops = malloc(bytes);
                if (slot->trace.ops == NULL) {
                    slot->disabled = true;
                    add(&call->stats.trace_aborts,1);
                } else {
                    r->stats.metadata_bytes += bytes;
                    slot->trace.capacity = r->config.trace_max_instructions;
                    slot->trace.entry = *state;
                    slot->recording = true;
                    observer->recording = &slot->trace;
                    call->recording = i;
                    call->recording_start = now_ns();
                    add(&call->stats.trace_recordings,1);
                }
            } else if (!r->config.trace_record_only) {
                compile_trace(call,slot);
                call->views[i].function = slot->function;
            }
        }
        if (slot->disabled || t->invocations <= slot->retry_after) call->blocked[i] = true;
        stop_observing(observer);
        pthread_mutex_unlock(&r->mutex);
        return;
    }
}

static void merge_trace_stats(struct rc_function_stats *to, const struct rc_function_stats *from)
{
    add(&to->trace_recordings,from->trace_recordings);
    add(&to->trace_aborts,from->trace_aborts);
    add(&to->trace_recorded_ops,from->trace_recorded_ops);
    add(&to->trace_recording_ns,from->trace_recording_ns);
    add(&to->trace_entries,from->trace_entries);
    add(&to->trace_side_exits,from->trace_side_exits);
    add(&to->trace_yields,from->trace_yields);
    add(&to->trace_iterations,from->trace_iterations);
    add(&to->trace_hits,from->trace_hits);
    add(&to->trace_misses,from->trace_misses);
    add(&to->trace_retirements,from->trace_retirements);
    add(&to->compilation_attempts,from->compilation_attempts);
    add(&to->compilations,from->compilations);
    add(&to->compilation_failures,from->compilation_failures);
    add(&to->compilation_ns,from->compilation_ns);
    if (from->generated_size != 0) to->generated_size = from->generated_size;
    if (from->compilation_attempts != 0) to->last_compile_error = from->last_compile_error;
}

static enum rc_status runtime_call(struct rc_runtime *r, rc_handle handle,
                                    const uint64_t *arguments, size_t count, bool integer,
                                    const struct rc_memory_view *view,
                                    uint64_t *result, struct rc_error *error)
{
    if (r == NULL || result == NULL ||
        (view != NULL && view->data == NULL && view->length != 0))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    struct rc_memory_view memory = view != NULL ? *view : (struct rc_memory_view){0};
    pthread_mutex_lock(&r->mutex);
    struct function *f = find_function(r, handle);
    if (f == NULL) {
        pthread_mutex_unlock(&r->mutex);
        return rc_fail(error, RC_NOT_FOUND, 0, 0);
    }
    struct translation *t = f->translation;
    if ((t->integer != NULL) != integer ||
        (integer && (count != t->integer->arguments || (count != 0 && arguments == NULL)))) {
        pthread_mutex_unlock(&r->mutex);
        return rc_fail(error,RC_ARGUMENT,0,0);
    }
    uint64_t argument = count > 0 ? arguments[0] : 0;
    uint64_t second = count > 1 ? arguments[1] : 0;

    if (f->active == SIZE_MAX || t->pins == SIZE_MAX || r->stats.active_calls == SIZE_MAX) {
        pthread_mutex_unlock(&r->mutex);
        return rc_fail(error, RC_CAPACITY, 0, 0);
    }
    ++f->active;
    ++r->stats.active_calls;
    add(&f->stats.invocations, 1);
    add(&r->stats.invocations, 1);
    add(&t->invocations, 1);
    bool tracing = r->config.policy == RC_POLICY_TRACE;
    struct trace_call trace_call;
    if (tracing) {
        trace_call = (struct trace_call){.runtime = r, .function = f};
        ++t->pins;
        for (size_t i = 0; i < t->trace_count; ++i) {
            trace_call.views[i].header = t->traces[i].trace.header;
            trace_call.blocked[i] = t->traces[i].disabled;
            if (t->invocations > t->traces[i].retry_after)
                trace_call.views[i].function = t->traces[i].function;
            if (trace_call.views[i].function != NULL) add(&trace_call.stats.trace_hits,1);
        }
        if (has_mapping(t)) touch(r,t);
    }
    uint64_t tick = 0;
    /* Every handle starts in the interpreter, even if an alias has cached code. */
    if (f->stats.interpreted != 0 && (t->code.function != NULL || t->integer_function != NULL)) {
        add(&f->stats.cache_hits, 1);
        add(&r->stats.cache_hits, 1);
    } else {
        add(&f->stats.cache_misses, 1);
        add(&r->stats.cache_misses, 1);
        if (r->config.policy == RC_POLICY_ADAPTIVE) tick = now_ns();
        if (t->code.function == NULL && t->integer_function == NULL && should_compile(r, f, tick))
            compile_translation(r, f);
    }
    rc_function native = f->stats.interpreted != 0 ? t->code.function : NULL;
    rc_integer_function integer_native = f->stats.interpreted != 0 ? t->integer_function : NULL;
    bool compiled = native != NULL || integer_native != NULL;
    bool sample = (!compiled ? f->stats.interpreted_samples : f->stats.compiled_samples) < 8 ||
        f->stats.invocations % r->config.sample_period == 0;
    if (compiled) {
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
    struct rc_error execution = {0};
    enum rc_status status = RC_OK;
    if (integer_native != NULL && t->integer->memory) {
        rc_integer_view_function entry;
        _Static_assert(sizeof(entry) == sizeof(integer_native), "native entry size");
        memcpy(&entry,&integer_native,sizeof(entry));
        struct rc_integer_result output;
        status = entry(argument,second,memory.data,memory.length,&output);
        if (status == RC_OK) value = output.value;
        else execution = (struct rc_error){status,output.offset,0};
    } else if (integer_native != NULL)
        value = integer_native(argument,second);
    else if (native != NULL)
        value = native(argument);
    else if (integer)
        status = rc_integer_interpret_view(t->integer,arguments,count,&memory,0,&value,&execution);
    else if (tracing) {
        struct rc_state state = {.argument = argument};
        struct rc_observer observer = {&trace_call,NULL,trace_before,trace_after};
        stop_observing(&observer);
        rc_interpret_observed(&t->program,&state,&observer);
        value = state.rax;
    } else
        (void)rc_interpret_validated(&t->program, argument, 0, &value);
    uint64_t duration = sample ? elapsed(start, now_ns()) : 0;

    pthread_mutex_lock(&r->mutex);
    if (tracing) {
        --t->pins;
        /* Each candidate exits at most once per call. Merge feedback under the
         * completion lock, retaining pins until all native snapshots are done. */
        for (size_t i = 0; i < t->trace_count; ++i) {
            uint32_t bit = UINT32_C(1) << i;
            if ((trace_call.exits & bit) == 0) continue;
            struct trace_slot *slot = &t->traces[i];
            if ((trace_call.short_exits & bit) != 0) ++slot->short_exits;
            else slot->short_exits = 0;
            if (slot->short_exits >= 8) {
                slot->retry_after = rc_saturating_add(t->invocations,r->config.retry_calls);
                slot->short_exits = 0;
                add(&trace_call.stats.trace_retirements,1);
            }
        }
        merge_trace_stats(&f->stats,&trace_call.stats);
    }
    if (compiled) {
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
    if (r->config.policy == RC_POLICY_ADAPTIVE && compiled &&
        f->slow_native_samples >= 8 && f->native_cost >
        rc_saturating_add(f->interpreted_cost, f->interpreted_cost / 4) && t->pins == 0) {
        (void)release_mapping(r, t, NULL);
        t->retry_after = rc_saturating_add(t->invocations, r->config.retry_calls);
        add(&f->stats.policy_postponed, 1);
        f->slow_native_samples = 0;
    }
    --f->active;
    --r->stats.active_calls;
    pthread_mutex_unlock(&r->mutex);
    if (status == RC_OK) *result = value;
    if (error != NULL) *error = execution;
    return status;
}

enum rc_status rc_runtime_call(struct rc_runtime *r, rc_handle handle,
                                uint64_t argument, uint64_t *result, struct rc_error *error)
{
    return runtime_call(r,handle,&argument,1,false,NULL,result,error);
}

enum rc_status rc_runtime_call_integer(struct rc_runtime *r, rc_handle handle,
                                        const uint64_t *arguments, size_t count,
                                        uint64_t *result, struct rc_error *error)
{
    return runtime_call(r,handle,arguments,count,true,NULL,result,error);
}

enum rc_status rc_runtime_call_integer_view(struct rc_runtime *r, rc_handle handle,
                                             const uint64_t *arguments, size_t count,
                                             const struct rc_memory_view *view,
                                             uint64_t *result, struct rc_error *error)
{
    return runtime_call(r,handle,arguments,count,true,view,result,error);
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
        f->translation->retry_after = rc_saturating_add(f->translation->invocations, r->config.retry_calls);
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

static void free_traces(struct rc_runtime *r, struct translation *t)
{
    if (t->traces == NULL) return;
    for (size_t i = 0; i < t->trace_count; ++i) {
        if (t->traces[i].trace.ops != NULL) {
            r->stats.metadata_bytes -= t->traces[i].trace.capacity * sizeof(struct rc_trace_op);
            free(t->traces[i].trace.ops);
        }
    }
    r->stats.metadata_bytes -= t->trace_count * sizeof(*t->traces);
    free(t->traces);
}

enum rc_status rc_runtime_remove(struct rc_runtime *r, rc_handle handle,
                                  struct rc_error *error)
{
    if (r == NULL)
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    pthread_mutex_lock(&r->mutex);
    struct function *f = find_function(r,handle);
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
            struct translation **entry = &r->buckets[t->hash % r->bucket_count].translation;
            while (*entry != t) entry = &(*entry)->hash_next;
            *entry = t->hash_next;
            unlink_translation(r,t);
            r->stats.program_bytes -= t->integer != NULL
                ? sizeof(*t->integer) + t->integer->count * sizeof(*t->integer->insns)
                : t->program.count * sizeof(*t->program.insns);
            r->stats.metadata_bytes -= sizeof(*t);
            --r->stats.translations;
            free_traces(r,t);
            rc_program_free(&t->program);
            rc_integer_program_destroy(t->integer);
            free(t);
        }
        struct function **entry = &r->buckets[f->handle % r->bucket_count].function;
        while (*entry != f) entry = &(*entry)->hash_next;
        *entry = f->hash_next;
        if (f->previous != NULL) f->previous->next = f->next;
        else r->functions = f->next;
        if (f->next != NULL) f->next->previous = f->previous;
        free(f);
        --r->stats.functions;
        r->stats.metadata_bytes -= sizeof(*f);
        if (r->stats.functions == 0) {
            r->stats.metadata_bytes -= r->bucket_count * sizeof(*r->buckets);
            free(r->buckets);
            r->buckets = NULL;
            r->bucket_count = 0;
        }
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
        free_traces(r,t);
        rc_program_free(&t->program);
        rc_integer_program_destroy(t->integer);
        free(t);
    }
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_destroy(&r->mutex);
    free(r->buckets);
    free(r);
    *runtime = NULL;
    return rc_fail(error, RC_OK, 0, 0);
}
