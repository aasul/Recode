#ifndef RECODE_H
#define RECODE_H

#include "recode_version.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum rc_status {
    RC_OK, RC_ARGUMENT, RC_MEMORY, RC_TRUNCATED, RC_UNSUPPORTED,
    RC_UNINITIALIZED, RC_MISSING_RET, RC_TRAILING, RC_INVALID_IR,
    RC_CAPACITY, RC_SYSTEM, RC_BRANCH, RC_UNDEFINED_ZF, RC_BUSY, RC_NOT_FOUND, RC_BUDGET, RC_BOUNDS
};

struct rc_error {
    enum rc_status status;
    size_t offset; /* Byte offset for decoding, instruction index for IR errors. */
    int system_errno;
};

enum rc_op {
    RC_INPUT, RC_CONST, RC_ADD, RC_SUB, RC_NOP, RC_RET,
    RC_CMP, RC_TEST, RC_JMP, RC_JZ, RC_JNZ
};

struct rc_insn {
    enum rc_op op;
    /* CONST zero-extends; ADD/SUB/CMP sign-extend raw imm32 bits.
     * JMP/JZ/JNZ instead store an absolute instruction index. */
    uint32_t immediate;
};

struct rc_program {
    struct rc_insn *insns;
    size_t count;
};

typedef uint64_t (*rc_function)(uint64_t);
struct rc_code {
    void *mapping;
    size_t size;
    rc_function function;
};

/* Compile-time macros describe headers; this describes the linked library. */
const char *rc_version_string(void);

const char *rc_status_string(enum rc_status status);
/* Decode and compile outputs must be zero-initialized and empty. Nonempty
 * outputs are rejected unchanged. error may be NULL; storage must not overlap. */
enum rc_status rc_decode(const uint8_t *bytes, size_t length,
                         struct rc_program *program, struct rc_error *error);
/* Release a decoded allocation or an empty object, never a borrowed IR array. */
void rc_program_free(struct rc_program *program);
enum rc_status rc_validate(const struct rc_program *program, struct rc_error *error);
enum rc_status rc_optimize(struct rc_program *program, struct rc_error *error);
/* Query size with bytes == NULL and capacity == 0. Insufficient capacity writes nothing. */
enum rc_status rc_emit(const struct rc_program *program, uint8_t *bytes,
                       size_t capacity, size_t *length, struct rc_error *error);
/* Failed rollback retains a mapping with function == NULL; release it with
 * rc_code_free. No executable function is returned on compilation failure. */
enum rc_status rc_compile(const uint8_t *bytes, size_t length, bool optimize,
                          struct rc_code *code, struct rc_error *error);
/* No calls may be in flight when releasing code. On failure the mapping stays owned. */
enum rc_status rc_code_free(struct rc_code *code, struct rc_error *error);

/* budget == 0 means unlimited. A positive budget counts executed IR instructions.
 * Invalid programs and exhausted budgets leave result unchanged. */
enum rc_status rc_interpret(const struct rc_program *program, uint64_t argument,
                             uint64_t budget, uint64_t *result, struct rc_error *error);

struct rc_runtime;
typedef uint64_t rc_handle; /* Nonzero, scoped to one runtime, never reused there. */
enum rc_policy { RC_POLICY_INTERPRET, RC_POLICY_FIXED, RC_POLICY_ADAPTIVE, RC_POLICY_TRACE };
struct rc_runtime_config {
    size_t executable_budget; /* All live page-rounded code mappings, including failed cleanup. */
    enum rc_policy policy;
    bool optimize;
    uint64_t hot_threshold;
    uint64_t retry_calls;
    uint64_t sample_period; /* Time every Nth call, and the first 8 calls in each tier. */
    uint64_t expected_reuse; /* Upper bound on adaptive prediction, not a promise. */
    uint64_t horizon_ns;
    uint64_t initial_compile_ns;
    uint64_t initial_native_ns;
    uint64_t trace_hot_threshold; /* Executed backward edges to a header. */
    size_t trace_max_instructions; /* 1..4096 operations per recording. */
    size_t trace_max_candidates; /* 1..16 headers per translation. */
    bool trace_record_only; /* Diagnostic recording without native compilation. */
};
struct rc_function_stats {
    uint64_t invocations, interpreted, jitted;
    uint64_t compilation_attempts, compilations, compilation_failures;
    uint64_t compilation_ns, interpreted_ns, compiled_ns; /* Execution totals cover timed samples only. */
    uint64_t interpreted_samples, compiled_samples;
    uint64_t cache_hits, cache_misses, policy_postponed;
    uint64_t timing_failures;
    size_t generated_size; /* Last successful compilation or cache hit. */
    uint64_t trace_recordings, trace_aborts, trace_recorded_ops, trace_recording_ns;
    uint64_t trace_entries, trace_side_exits, trace_yields, trace_iterations;
    uint64_t trace_hits, trace_misses, trace_retirements;
    struct rc_error last_compile_error; /* Nonfatal failures do not replace execution success. */
};
struct rc_runtime_stats {
    uint64_t invocations, cache_hits, cache_misses, evictions;
    uint64_t capacity_failures, release_failures, compilations, compilation_failures;
    size_t executable_bytes, peak_executable_bytes;
    size_t metadata_bytes, program_bytes; /* Persistent requested bytes, excludes allocator overhead. */
    size_t functions, translations, active_calls;
    struct rc_error last_cache_error; /* Last failed mapping release, including deferred cleanup. */
};

struct rc_runtime_config rc_runtime_default_config(void);
/* out must point to NULL. Configuration is copied and immutable. */
enum rc_status rc_runtime_create(const struct rc_runtime_config *config,
                                  struct rc_runtime **out, struct rc_error *error);
/* Source bytes are borrowed only during registration; validated IR is owned. */
enum rc_status rc_runtime_register(struct rc_runtime *runtime, const uint8_t *bytes,
                                    size_t length, rc_handle *out, struct rc_error *error);
enum rc_status rc_runtime_call(struct rc_runtime *runtime, rc_handle handle,
                                uint64_t argument, uint64_t *result, struct rc_error *error);
/* Removal returns BUSY without removing an in-flight handle. Release failure
 * retains registration and ownership for retry. Successful removal invalidates it. */
enum rc_status rc_runtime_remove(struct rc_runtime *runtime, rc_handle handle,
                                  struct rc_error *error);
/* Invalidate all handles sharing this translation. BUSY leaves it callable.
 * Release failure disables native lookup but retains mapping ownership. */
enum rc_status rc_runtime_invalidate(struct rc_runtime *runtime, rc_handle handle,
                                      struct rc_error *error);
/* Clears all unpinned mappings; returns BUSY if some remain pinned. Retry failures. */
enum rc_status rc_runtime_clear(struct rc_runtime *runtime, struct rc_error *error);
enum rc_status rc_runtime_function_stats(struct rc_runtime *runtime, rc_handle handle,
                                          struct rc_function_stats *out, struct rc_error *error);
enum rc_status rc_runtime_get_stats(struct rc_runtime *runtime,
                                     struct rc_runtime_stats *out, struct rc_error *error);
/* Caller must exclude all other API entry during destruction. In-flight calls
 * return BUSY. Release failure retains a usable runtime for retry. Success frees
 * everything and sets *runtime = NULL. No raw native pointers escape the API.
 * Other runtime operations are thread-safe, not signal-safe or cancellation-safe. */
enum rc_status rc_runtime_destroy(struct rc_runtime **runtime, struct rc_error *error);

#endif
