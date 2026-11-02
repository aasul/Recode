#define _POSIX_C_SOURCE 200809L
#include "integer.h"
#include "bench.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); }
}
uint64_t bench_now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void row(const char *name, const char *phase, size_t count, uint64_t ns, size_t bytes, size_t batch)
{
    printf("%s,%s,%zu,%" PRIu64 ",%.3f,%zu,%zu\n",name,phase,count,ns,(double)ns/(double)count,bytes,batch);
}
int main(int argc, char **argv)
{
    if (argc > 3) return 1;
    puts("workload,phase,count,total_ns,ns_per_item,code_bytes,batch_size");
    const size_t counts[] = {1,8,32,128,1024,4096,65536};
    for (unsigned kind = 0; kind < 6; ++kind) {
        struct workload w = workload(kind);
        struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
        require(rc_builder_create(8,2,&b,NULL));
        for (size_t n = 0; n < w.count; ++n) require(rc_builder_append(b,w.insns[n],NULL));
        require(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
        size_t length; require(rc_integer_emit(p,NULL,0,&length,NULL));
        uint8_t *bytes = malloc(length); if (bytes == NULL) return 1;
        uint64_t start = bench_now();
        for (unsigned n = 0; n < 100; ++n) require(rc_integer_validate(p,NULL));
        row(w.name,"validate",100,bench_now()-start,length,0);
        start = bench_now();
        for (unsigned n = 0; n < 100; ++n) require(rc_integer_emit(p,NULL,0,&length,NULL));
        row(w.name,"query",100,bench_now()-start,length,0);
        start = bench_now();
        for (unsigned n = 0; n < 100; ++n) require(rc_integer_emit(p,bytes,length,&length,NULL));
        row(w.name,"emit",100,bench_now()-start,length,0);
        if (argc >= 2) {
            char path[4096];
            int used = snprintf(path,sizeof(path),"%s/%s.bin",argv[1],w.name);
            if (used < 0 || (size_t)used >= sizeof(path)) return 1;
            FILE *f = fopen(path,"wb");
            if (f == NULL || fwrite(bytes,1,length,f) != length || fclose(f) != 0) return 1;
        }
        free(bytes);
        struct rc_code code = {0}; rc_integer_function raw; bool allocated;
        size_t page = (size_t)sysconf(_SC_PAGESIZE), mapped = (length+page-1)/page*page;
        start = bench_now();
        for (unsigned n = 0; n < 100; ++n) {
            require(rc_integer_map(p,mapped,&code,&raw,&allocated,NULL));
            require(rc_code_free(&code,NULL));
        }
        row(w.name,"map_emit_publish_free",100,bench_now()-start,length,0);
        require(rc_integer_map(p,mapped,&code,&raw,&allocated,NULL));
        rc_integer_view_function memory; memcpy(&memory,&raw,sizeof(memory));
        struct rc_runtime_config config = rc_runtime_default_config();
        config.policy = RC_POLICY_FIXED; config.hot_threshold = 2;
        struct rc_runtime *r = NULL; rc_handle h;
        require(rc_runtime_create(&config,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
        for (size_t c = 0; c < sizeof(counts)/sizeof(*counts); ++c) {
            size_t count = counts[c], repeats = 131072/count;
            struct input *in = malloc(count*sizeof(*in));
            struct rc_memory_view *views = malloc(count*sizeof(*views));
            struct rc_integer_batch_item *items = malloc(count*sizeof(*items));
            uint64_t *out = malloc(count*sizeof(*out)), *expected = malloc(count*sizeof(*expected));
            if (in == NULL || views == NULL || items == NULL || out == NULL || expected == NULL) return 1;
            for (size_t n = 0; n < count; ++n) {
                inputs(n,&in[n]); views[n] = (struct rc_memory_view){in[n].data,sizeof(in[n].data)};
                items[n] = (struct rc_integer_batch_item){in[n].args,2,&views[n]};
                expected[n] = reference(kind,&in[n]);
            }
            for (unsigned order = 0; order < 5; ++order) {
                unsigned mode = (order + (argc == 3 ? (unsigned)strtoul(argv[2],NULL,10) % 5 : 0)) % 5;
                static const char *names[] = {"c","interpreter","kernel","scalar","batch"};
                size_t done;
                for (unsigned warm = 0; warm < 2; ++warm)
                    require(rc_runtime_call_integer_batch(r,h,items,count,out,count,&done,NULL));
                start = bench_now();
                for (size_t repeat = 0; repeat < repeats; ++repeat) {
                    if (mode == 4) {
                        require(rc_runtime_call_integer_batch(r,h,items,count,out,count,&done,NULL));
                        if (done != count) return 1;
                    } else for (size_t n = 0; n < count; ++n) {
                        if (mode == 0) out[n] = reference(kind,&in[n]);
                        else if (mode == 1) require(rc_integer_interpret_view(p,in[n].args,2,&views[n],0,&out[n],NULL));
                        else if (mode == 2 && p->memory) {
                            struct rc_integer_result result;
                            require(memory(in[n].args[0],in[n].args[1],in[n].data,sizeof(in[n].data),&result));
                            out[n] = result.value;
                        } else if (mode == 2) out[n] = raw(in[n].args[0],in[n].args[1]);
                        else require(rc_runtime_call_integer_view(r,h,in[n].args,2,&views[n],&out[n],NULL));
                    }
                }
                uint64_t duration = bench_now()-start;
                for (size_t n = 0; n < count; ++n) if (out[n] != expected[n]) return 1;
                row(w.name,names[mode],count*repeats,duration,length,count);
            }
            free(expected); free(out); free(items); free(views); free(in);
        }
        require(rc_runtime_destroy(&r,NULL)); require(rc_code_free(&code,NULL)); rc_integer_program_destroy(p);
    }
    return 0;
}
