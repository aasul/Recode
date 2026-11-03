#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void require(enum rc_status s) { if (s != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(s)); exit(1); } }
uint64_t bench_now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
int main(void)
{
    const size_t sizes[] = {1,8,32,128,1024,4096,65536};
    puts("round,workload,items,opt_in,registration_ns,execution_ns,cleanup_ns,total_ns,interpreted,jitted,compilation_ns,code_bytes,executable_bytes");
    for (unsigned kind = 0; kind < 6; ++kind) {
        struct workload w = workload(kind); struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
        require(rc_builder_create(8,2,&b,NULL));
        for (size_t n = 0; n < w.count; ++n) require(rc_builder_append(b,w.insns[n],NULL));
        require(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
        for (size_t c = 0; c < sizeof(sizes)/sizeof(*sizes); ++c) {
            size_t count = sizes[c];
            struct input *in = malloc(count*sizeof(*in));
            struct rc_memory_view *views = malloc(count*sizeof(*views));
            struct rc_integer_batch_item *items = malloc(count*sizeof(*items));
            uint64_t *out = malloc(count*sizeof(*out));
            if (!in || !views || !items || !out) return 1;
            for (size_t n = 0; n < count; ++n) {
                inputs(n,&in[n]); views[n] = (struct rc_memory_view){in[n].data,sizeof(in[n].data)};
                items[n] = (struct rc_integer_batch_item){in[n].args,2,&views[n]};
            }
            for (unsigned round = 0; round < 7; ++round) for (unsigned order = 0; order < 2; ++order) {
                bool enabled = (round+order)%2 != 0;
                struct rc_runtime_config config = rc_runtime_default_config(); config.policy = RC_POLICY_FIXED;
#ifdef RECODE_HAS_COLD_BATCH
                config.cold_batch_compile = enabled;
#endif
                struct rc_runtime *r = NULL; rc_handle h;
                uint64_t start = bench_now();
                require(rc_runtime_create(&config,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
                uint64_t registered = bench_now(); size_t done;
                require(rc_runtime_call_integer_batch(r,h,items,count,out,count,&done,NULL));
                uint64_t executed = bench_now();
                if (done != count) return 1;
                struct rc_function_stats stats; struct rc_runtime_stats runtime;
                require(rc_runtime_function_stats(r,h,&stats,NULL)); require(rc_runtime_get_stats(r,&runtime,NULL));
                uint64_t cleanup = bench_now(); require(rc_runtime_destroy(&r,NULL)); cleanup = bench_now()-cleanup;
                for (size_t n = 0; n < count; ++n) if (out[n] != reference(kind,&in[n])) return 1;
                printf("%u,%s,%zu,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu,%zu\n",
                    round+1,w.name,count,enabled,registered-start,executed-registered,cleanup,
                    executed-start+cleanup,stats.interpreted,stats.jitted,stats.compilation_ns,stats.generated_size,runtime.executable_bytes);
            }
            free(out); free(items); free(views); free(in);
        }
        rc_integer_program_destroy(p);
    }
    return 0;
}
