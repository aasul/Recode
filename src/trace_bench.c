#define _POSIX_C_SOURCE 200809L
#include "trace.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"benchmark: %s\n",rc_status_string(status)); exit(1); }
}
static uint64_t zero(uint64_t x) { (void)x; return 0; }
static uint64_t paths(uint64_t x) { return x >= 5 ? x : 0; }
static uint64_t increment(uint64_t x) { return x + 1; }
static bool recorded_before(struct rc_observer *o, struct rc_state *s)
{
    (void)s; return o->recording == NULL;
}
static void recorded_after(struct rc_observer *o, size_t pc, struct rc_state *s, bool returned)
{
    (void)pc; (void)s; (void)returned;
    if (o->recording->stop != RC_TRACE_RECORDING) o->recording = NULL;
}

static void workload(const char *name, struct rc_program p, rc_function expected, bool loop)
{
    uint8_t bytes[4096];
    size_t length;
    require(rc_emit(&p,bytes,sizeof(bytes),&length,NULL));
    const char *modes[] = {"direct-c","interpret","whole-jit","fixed-runtime",
        "record-only","trace-cold","trace-warm","trace-kernel"};
    const uint64_t counts[] = {1,32,20000};
    for (size_t n = 0; n < 3; ++n) {
        uint64_t calls = counts[n], checked = 0;
        for (uint64_t i = 0; i < calls; ++i) checked += expected(1 + i % 256);
        for (unsigned round = 0; round < 5; ++round) {
            for (unsigned order = 0; order < 8; ++order) {
                unsigned mode = (order + round) % 8;
                if (mode == 7 && !loop) continue;
                struct rc_runtime *r = NULL;
                struct rc_code code = {0};
                rc_trace_function native_trace = NULL;
                struct rc_function_stats fs = {0};
                struct rc_runtime_stats rs = {0};
                rc_handle h = 0;
                uint64_t total = now(), compile_ns = 0, record_ns = 0;
                size_t generated = 0;
                if (mode == 2) {
                    uint64_t start = now();
                    require(rc_compile(bytes + 4,length - 4,true,&code,NULL));
                    compile_ns = now() - start;
                    generated = code.size;
                } else if (mode >= 3 && mode <= 6) {
                    struct rc_runtime_config c = rc_runtime_default_config();
                    c.policy = mode == 3 ? RC_POLICY_FIXED : RC_POLICY_TRACE;
                    c.trace_record_only = mode == 4;
                    require(rc_runtime_create(&c,&r,NULL));
                    require(rc_runtime_register(r,bytes + 4,length - 4,&h,NULL));
                    if (mode == 6) {
                        for (unsigned i = 0; i < 8; ++i) {
                            uint64_t value;
                            require(rc_runtime_call(r,h,256,&value,NULL));
                            if (value != expected(256)) exit(1);
                        }
                    }
                } else if (mode == 7) {
                    struct rc_trace_op ops[256];
                    struct rc_trace trace = {.ops = ops,.capacity = 256,.header = 1,
                        .entry = {.rax = 256,.argument = 256,.pc = 1}};
                    struct rc_state state = trace.entry;
                    struct rc_observer observer = {NULL,&trace,recorded_before,recorded_after};
                    uint64_t start = now();
                    rc_interpret_observed(&p,&state,&observer);
                    record_ns = now() - start;
                    start = now();
                    require(rc_trace_emit(&p,&trace,true,NULL,0,&generated,NULL));
                    bool allocated;
                    require(rc_trace_map(&p,&trace,true,generated,&code,&native_trace,&allocated,NULL));
                    compile_ns = now() - start;
                }
                uint64_t start = now(), checksum = 0;
                rc_function volatile direct = expected;
                for (uint64_t i = 0; i < calls; ++i) {
                    uint64_t x = 1 + i % 256, value;
                    if (mode == 0) value = direct(x);
                    else if (mode == 1) require(rc_interpret_validated(&p,x,0,&value));
                    else if (mode == 2) value = code.function(x);
                    else if (mode == 7) {
                        struct rc_state state = {.rax = x,.argument = x,.pc = 1,.remaining = RC_TRACE_QUANTUM};
                        enum rc_trace_exit reason = native_trace(&state);
                        ++fs.trace_entries;
                        fs.trace_iterations += RC_TRACE_QUANTUM - state.remaining;
                        if (reason == RC_TRACE_SIDE_EXIT) ++fs.trace_side_exits;
                        if (reason != RC_TRACE_RETURNED) {
                            struct rc_observer o = {0};
                            rc_interpret_observed(&p,&state,&o);
                        }
                        value = state.rax;
                    } else require(rc_runtime_call(r,h,x,&value,NULL));
                    checksum += value;
                }
                uint64_t end = now();
                if (checksum != checked) { fputs("checksum mismatch\n",stderr); exit(1); }
                if (r != NULL) {
                    require(rc_runtime_function_stats(r,h,&fs,NULL));
                    require(rc_runtime_get_stats(r,&rs,NULL));
                    compile_ns = fs.compilation_ns; record_ns = fs.trace_recording_ns;
                    generated = fs.generated_size;
                }
                printf("run,%s,%s,calls,%" PRIu64 ",round,%u,ns_per_call,%.3f,total_us,%.3f,"
                    "compile_ns,%" PRIu64 ",record_ns,%" PRIu64 ",generated,%zu,entries,%" PRIu64
                    ",iterations,%" PRIu64 ",side_exits,%" PRIu64 ",hits,%" PRIu64 ",misses,%" PRIu64
                    ",evictions,%" PRIu64 ",mapping,%zu,metadata,%zu,program,%zu,checksum,%" PRIu64 "\n",
                    name,modes[mode],calls,round+1,(double)(end-start)/(double)calls,(double)(end-total)/1000,
                    compile_ns,record_ns,generated,fs.trace_entries,fs.trace_iterations,fs.trace_side_exits,
                    fs.trace_hits,fs.trace_misses,rs.evictions,rs.peak_executable_bytes,
                    rs.metadata_bytes,rs.program_bytes,checksum);
                if (r != NULL) require(rc_runtime_destroy(&r,NULL));
                require(rc_code_free(&code,NULL));
            }
        }
    }
}
static void pressure(void)
{
    for (size_t pages = 1; pages <= 16; pages *= 4) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = RC_POLICY_TRACE; c.trace_hot_threshold = 2;
        c.executable_budget = pages * (size_t)sysconf(_SC_PAGESIZE);
        struct rc_runtime *r = NULL;
        require(rc_runtime_create(&c,&r,NULL));
        uint64_t start = now();
        for (uint32_t i = 0; i < 4096; ++i) {
            struct rc_insn insns[] = {{RC_CONST,4 + i % 256},{RC_SUB,1},
                {RC_JNZ,1},{RC_CONST,i},{RC_RET,0}};
            struct rc_program p = {insns,5};
            uint8_t bytes[64]; size_t length; rc_handle h;
            require(rc_emit(&p,bytes,sizeof(bytes),&length,NULL));
            require(rc_runtime_register(r,bytes+4,length-4,&h,NULL));
            for (unsigned j = 0; j < 3; ++j) {
                uint64_t value;
                require(rc_runtime_call(r,h,j,&value,NULL));
                if (value != i) exit(1);
            }
        }
        uint64_t end = now();
        struct rc_runtime_stats s;
        require(rc_runtime_get_stats(r,&s,NULL));
        if (s.executable_bytes > c.executable_budget || s.compilations != 4096 || s.evictions != 4096-pages)
            exit(1);
        printf("pressure,pages,%zu,total_ms,%.3f,compilations,%" PRIu64 ",evictions,%" PRIu64
            ",peak_mapping,%zu,metadata,%zu,program,%zu\n",pages,(double)(end-start)/1000000,
            s.compilations,s.evictions,s.peak_executable_bytes,s.metadata_bytes,s.program_bytes);
        require(rc_runtime_clear(r,NULL));
        require(rc_runtime_get_stats(r,&s,NULL));
        if (s.executable_bytes != 0) exit(1);
        require(rc_runtime_destroy(&r,NULL));
    }
}
static void exit_probe(void)
{
    struct rc_insn insns[] = {{RC_INPUT,0},{RC_SUB,1},{RC_JNZ,1},{RC_RET,0}};
    struct rc_program p = {insns,4};
    struct rc_trace_op ops[] = {{{RC_SUB,1},1,2,0,false},{{RC_JNZ,1},2,1,3,true}};
    struct rc_trace trace = {.ops = ops,.count = 2,.capacity = 2,.header = 1,
        .entry = {.pc = 1},.stop = RC_TRACE_LOOP};
    size_t size;
    require(rc_trace_emit(&p,&trace,true,NULL,0,&size,NULL));
    struct rc_code code = {0};
    bool allocated;
    rc_trace_function fn;
    require(rc_trace_map(&p,&trace,true,size,&code,&fn,&allocated,NULL));
    for (unsigned round = 0; round < 5; ++round) {
        for (unsigned guard = 0; guard < 2; ++guard) {
            uint64_t start = now();
            for (unsigned i = 0; i < 200000; ++i) {
                struct rc_state state = {.rax = guard != 0 ? 1 : 2,.pc = 1,.remaining = 1};
                enum rc_trace_exit reason = fn(&state);
                if (reason != (guard != 0 ? RC_TRACE_SIDE_EXIT : RC_TRACE_YIELD) ||
                    state.rax != (guard != 0 ? 0U : 1U) || state.zf != (guard != 0) ||
                    state.pc != (guard != 0 ? 3U : 1U)) exit(1);
            }
            printf("exit-probe,%s,round,%u,ns_per_call,%.3f,generated,%zu\n",
                guard != 0 ? "guard" : "yield",round+1,(double)(now()-start)/200000,size);
        }
    }
    require(rc_code_free(&code,NULL));
}

int main(int argc, char **argv)
{
    printf("compiler,%s,clock,CLOCK_MONOTONIC,rounds,%u,page_size,%ld\n",
        __VERSION__,argc == 1 ? 5U : 1U,sysconf(_SC_PAGESIZE));
    if (argc == 2 && strcmp(argv[1],"--pressure") == 0) { pressure(); return 0; }
    if (argc != 1) return 1;
    struct rc_insn countdown[] = {{RC_INPUT,0},{RC_SUB,1},{RC_JNZ,1},{RC_RET,0}};
    struct rc_insn predictable[] = {{RC_INPUT,0},{RC_CMP,0},{RC_JZ,7},
        {RC_ADD,7},{RC_SUB,6},{RC_SUB,2},{RC_JNZ,1},{RC_RET,0}};
    struct rc_insn changing[] = {{RC_INPUT,0},{RC_CMP,128},{RC_JZ,6},
        {RC_SUB,1},{RC_JNZ,1},{RC_RET,0},{RC_SUB,1},{RC_JNZ,1},{RC_RET,0}};
    struct rc_insn multi[] = {{RC_INPUT,0},{RC_TEST,0},{RC_JZ,8},{RC_CMP,5},
        {RC_JZ,10},{RC_SUB,1},{RC_JNZ,1},{RC_RET,0},{RC_CONST,9},{RC_RET,0},{RC_INPUT,0},{RC_RET,0}};
    struct rc_insn short_lived[] = {{RC_INPUT,0},{RC_ADD,1},{RC_RET,0}};
    exit_probe();
    workload("countdown",(struct rc_program){countdown,4},zero,true);
    workload("predictable",(struct rc_program){predictable,8},zero,true);
    workload("changing",(struct rc_program){changing,9},zero,true);
    workload("paths",(struct rc_program){multi,12},paths,true);
    workload("short",(struct rc_program){short_lived,3},increment,false);
    return 0;
}
