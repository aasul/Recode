#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void require(enum rc_status s) { if (s != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(s)); exit(1); } }
uint64_t bench_now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static void prepare(const uint8_t *records, size_t count, size_t stride, uint64_t (*args)[2],
                    struct rc_memory_view *views, struct rc_integer_batch_item *items)
{
    for (size_t n = 0; n < count; ++n) {
        memcpy(args[n],records+n*stride+1,16);
        views[n] = (struct rc_memory_view){records+n*stride+17,16};
        items[n] = (struct rc_integer_batch_item){args[n],2,views+n};
    }
}
static void execute(unsigned mode, unsigned kind, struct rc_runtime *r, rc_handle h,
                     const struct rc_integer_record_batch *layout,
                     const struct rc_integer_batch_item *items, uint64_t *out)
{
    size_t count = layout->record_count, done;
    if (mode == 2) require(rc_runtime_call_integer_batch(r,h,items,count,out,count,&done,NULL));
    else if (mode == 3) require(rc_runtime_call_integer_records(r,h,layout,out,count,&done,NULL));
    else for (size_t n = 0; n < count; ++n) {
        const uint8_t *record = (const uint8_t *)layout->records+n*layout->stride;
        struct input in; memcpy(in.args,record+1,16); memcpy(in.data,record+17,16);
        struct rc_memory_view view = {record+17,16};
        if (mode == 0) out[n] = reference(kind,&in);
        else require(rc_runtime_call_integer_view(r,h,in.args,2,&view,out+n,NULL));
    }
    if (mode >= 2 && done != count) exit(1);
}
int main(void)
{
    const size_t counts[] = {1,8,32,128,1024,4096,65536}, strides[] = {33,64,129};
    const char *names[] = {"c","scalar","descriptor","records"};
    puts("round,workload,items,stride,path,preparation_ns,repeats,execution_ns,ns_per_record,one_request_with_preparation_ns,descriptor_bytes,code_bytes,compilation_ns");
    for (unsigned kind = 0; kind < 6; ++kind) {
        struct workload w = workload(kind); struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
        require(rc_builder_create(8,2,&b,NULL));
        for (size_t n = 0; n < w.count; ++n) require(rc_builder_append(b,w.insns[n],NULL));
        require(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
        for (size_t s = 0; s < 3; ++s) for (size_t c = 0; c < 7; ++c) {
            size_t count = counts[c], stride = strides[s], repeats = 65536/count;
            uint8_t *records = calloc(count,stride); uint64_t (*args)[2] = malloc(count*sizeof(*args));
            struct rc_memory_view *views = malloc(count*sizeof(*views));
            struct rc_integer_batch_item *items = malloc(count*sizeof(*items));
            uint64_t *out = malloc(count*sizeof(*out)), *expected = malloc(count*sizeof(*expected));
            if (!records || !args || !views || !items || !out || !expected) return 1;
            for (size_t n = 0; n < count; ++n) {
                struct input in; inputs(n,&in); memcpy(records+n*stride+1,in.args,16);
                memcpy(records+n*stride+17,in.data,16); expected[n] = reference(kind,&in);
            }
            uint64_t start = bench_now(); prepare(records,count,stride,args,views,items); uint64_t prep = bench_now()-start;
            struct rc_integer_record_batch layout = {records,count,stride,33,2,{1,9},17,16};
            struct rc_runtime_config config = rc_runtime_default_config(); config.policy = RC_POLICY_FIXED; config.hot_threshold = 2;
            struct rc_runtime *r = NULL; rc_handle h;
            require(rc_runtime_create(&config,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
            for (unsigned warm = 0; warm < 16; ++warm) execute(3,kind,r,h,&layout,items,out);
            for (unsigned round = 0; round < 3; ++round) for (unsigned order = 0; order < 4; ++order) {
                unsigned mode = (round+order)%4; start = bench_now();
                for (size_t n = 0; n < repeats; ++n) execute(mode,kind,r,h,&layout,items,out);
                uint64_t duration = bench_now()-start;
                for (size_t n = 0; n < count; ++n) if (out[n] != expected[n]) return 1;
                start = bench_now(); if (mode == 2) prepare(records,count,stride,args,views,items);
                execute(mode,kind,r,h,&layout,items,out); uint64_t request = bench_now()-start;
                for (size_t n = 0; n < count; ++n) if (out[n] != expected[n]) return 1;
                struct rc_function_stats stats; require(rc_runtime_function_stats(r,h,&stats,NULL));
                printf("%u,%s,%zu,%zu,%s,%" PRIu64 ",%zu,%" PRIu64 ",%.3f,%" PRIu64 ",%zu,%zu,%" PRIu64 "\n",
                    round+1,w.name,count,stride,names[mode],mode == 2 ? prep : 0,repeats,duration,
                    (double)duration/(double)(count*repeats),request,mode == 2 ? count*(sizeof(*items)+sizeof(*views)+sizeof(*args)) : 0,
                    stats.generated_size,stats.compilation_ns);
            }
            require(rc_runtime_destroy(&r,NULL)); free(expected); free(out); free(items); free(views); free(args); free(records);
        }
        rc_integer_program_destroy(p);
    }
    return 0;
}
