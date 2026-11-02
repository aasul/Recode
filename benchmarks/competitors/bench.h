#ifndef COMPARISON_BENCH_H
#define COMPARISON_BENCH_H
#include "recode_integer.h"
#include <stdint.h>

struct workload {
    const char *name;
    struct rc_integer_insn insns[64];
    size_t count;
    unsigned kind;
};
struct input { uint64_t args[2]; uint8_t data[16]; };
typedef uint64_t (*entry_fn)(uint64_t, uint64_t, const void *, uint64_t);
struct metrics {
    uint64_t init_ns, build_ns, validate_ns, register_ns, compile_ns, first_ns;
    int64_t code_bytes, executable_bytes;
    uint64_t interpreted, jitted, evictions;
};
#if RECODE_VERSION_MAJOR > 1 || (RECODE_VERSION_MAJOR == 1 && RECODE_VERSION_MINOR >= 5)
#define BENCH_HAS_BATCH 1
#endif
struct engine;
uint64_t bench_now(void);
void require(enum rc_status status);
void inputs(size_t n, struct input *in);
uint64_t reference(unsigned kind, const struct input *in);
struct workload workload(unsigned kind);
struct engine *engine_open(const char *mode, struct metrics *m);
void engine_build(struct engine *e, const struct workload *w, struct metrics *m);
uint64_t engine_call(struct engine *e, const struct input *in);
void engine_prepare_bulk(struct engine *e, const struct input *inputs, size_t count);
void engine_bulk(struct engine *e, const struct input *inputs, size_t count, uint64_t *results);
void engine_stats(struct engine *e, struct metrics *m);
void engine_close(struct engine *e);
#ifdef HAVE_LLVM
void *llvm_open(void);
entry_fn llvm_build(void *state, const struct workload *w, struct metrics *m);
void llvm_close(void *state);
#endif
#ifdef HAVE_MIR
void *mir_open(void);
entry_fn mir_build(void *state, const struct workload *w, struct metrics *m);
void mir_close(void *state);
#endif
#endif
