#define _POSIX_C_SOURCE 200809L
#include "integer.h"
#include "bench.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
void require(enum rc_status s) { if (s != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(s)); exit(1); } }
uint64_t bench_now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
int main(void)
{
    const size_t counts[] = {1,8,32,128,1024,4096,65536};
    puts("workload,items,path,repeats,native_entries,total_ns,ns_per_record,text_bytes,compile_publish_ns");
    for (unsigned kind = 0; kind < 6; ++kind) {
        struct workload w = workload(kind); struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
        require(rc_builder_create(8,2,&b,NULL));
        for (size_t n = 0; n < w.count; ++n) require(rc_builder_append(b,w.insns[n],NULL));
        require(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
        struct rc_integer_plan plan; uint64_t start = bench_now();
#ifdef RECODE_HAS_FUSION
        require(rc_integer_prepare_batch(p,&plan,NULL));
#else
        require(rc_integer_prepare(p,&plan,NULL));
#endif
        size_t page = (size_t)sysconf(_SC_PAGESIZE), mapped = (plan.length+page-1)/page*page;
        struct rc_code code = {0}; rc_integer_function raw; bool allocated;
        require(rc_integer_map_prepared(&plan,mapped,&code,&raw,&allocated,NULL));
        uint64_t compile = bench_now()-start;
        rc_integer_view_function memory; memcpy(&memory,&raw,sizeof(memory));
#ifdef RECODE_HAS_FUSION
        void *entry = (uint8_t *)code.mapping+plan.batch;
        rc_integer_batch_function fused; memcpy(&fused,&entry,sizeof(fused));
#endif
        for (size_t c = 0; c < 7; ++c) {
            size_t count = counts[c], repeats = 131072/count;
            struct input *in = malloc(count*sizeof(*in)); struct rc_memory_view *views = malloc(count*sizeof(*views));
            struct rc_integer_batch_item *items = malloc(count*sizeof(*items)); uint64_t *out = malloc(count*sizeof(*out));
            if (!in || !views || !items || !out) return 1;
            for (size_t n = 0; n < count; ++n) {
                inputs(n,&in[n]); views[n] = (struct rc_memory_view){in[n].data,sizeof(in[n].data)};
                items[n] = (struct rc_integer_batch_item){in[n].args,2,views+n};
            }
#ifdef RECODE_HAS_FUSION
            unsigned modes = 3;
            struct rc_integer_record_batch layout = {in,count,sizeof(*in),sizeof(*in),2,{0,8},16,16};
#else
            unsigned modes = 1;
#endif
            for (unsigned mode = 0; mode < modes; ++mode) {
                uint64_t entries = 0; start = bench_now();
                for (size_t repeat = 0; repeat < repeats; ++repeat) {
#ifdef RECODE_HAS_FUSION
                    if (mode != 0) {
                        struct rc_integer_batch_context context = {mode == 1 ? (const void *)items : (const void *)in,
                            out,count,0,0,mode == 1 ? NULL : &layout,mode == 1 ? sizeof(*items) : sizeof(*in)};
                        ++entries; require(fused(&context)); if (context.completed != count) return 1;
                        continue;
                    }
#endif
                    for (size_t n = 0; n < count; ++n) {
                        ++entries;
                        if (p->memory) {
                            struct rc_integer_result result;
                            require(memory(in[n].args[0],in[n].args[1],in[n].data,sizeof(in[n].data),&result)); out[n] = result.value;
                        } else out[n] = raw(in[n].args[0],in[n].args[1]);
                    }
                }
                uint64_t duration = bench_now()-start;
                for (size_t n = 0; n < count; ++n) if (out[n] != reference(kind,&in[n])) return 1;
                printf("%s,%zu,%s,%zu,%" PRIu64 ",%" PRIu64 ",%.3f,%zu,%" PRIu64 "\n",w.name,count,
                    mode == 0 ? "scalar" : mode == 1 ? "fused-descriptor" : "fused-records",repeats,entries,duration,
                    (double)duration/(double)(count*repeats),plan.length,compile);
            }
            free(out); free(items); free(views); free(in);
        }
        require(rc_code_free(&code,NULL)); rc_integer_program_destroy(p);
    }
    return 0;
}
