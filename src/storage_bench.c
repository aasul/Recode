#define _POSIX_C_SOURCE 200809L
#include "recode_integer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static void require(enum rc_status s) { if (s != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(s)); exit(1); } }
static uint64_t now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static long rss(void)
{
    FILE *f = fopen("/proc/self/status","r"); if (!f) exit(1);
    char line[256]; long value = -1;
    while (fgets(line,sizeof(line),f)) if (sscanf(line,"VmHWM: %ld kB",&value) == 1) break;
    if (fclose(f) != 0 || value < 0) exit(1);
    return value;
}
int main(void)
{
    const size_t sizes[] = {1,16,64,256,1024,4096};
    puts("functions,registration_ns,compilation_ns,lookup_execution_ns,cleanup_ns,text_bytes,occupied_bytes,slab_free_bytes,committed_bytes,peak_bytes,mappings,evictions,failures,rss_kib");
    for (size_t c = 0; c < sizeof(sizes)/sizeof(*sizes); ++c) {
        size_t count = sizes[c], page = (size_t)sysconf(_SC_PAGESIZE);
        rc_handle *handles = malloc(count*sizeof(*handles)); if (!handles) return 1;
        struct rc_runtime_config config = rc_runtime_default_config(); config.policy = RC_POLICY_FIXED;
        config.hot_threshold = 2; config.executable_budget = 64*1024*1024;
        struct rc_runtime *r = NULL; require(rc_runtime_create(&config,&r,NULL));
        uint64_t start = now();
        for (size_t n = 0; n < count; ++n) {
            struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
            require(rc_builder_create(1,0,&b,NULL));
            require(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_CONSTANT,.immediate=n},NULL));
            require(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL));
            require(rc_builder_finalize(b,&p,NULL)); require(rc_runtime_register_integer(r,p,handles+n,NULL));
            rc_integer_program_destroy(p); rc_builder_destroy(b);
        }
        uint64_t registered = now()-start, compilation = 0; size_t text = 0;
        for (size_t n = 0; n < count; ++n) {
            uint64_t value;
            for (unsigned call = 0; call < 2; ++call) {
                require(rc_runtime_call_integer(r,handles[n],NULL,0,&value,NULL)); if (value != n) return 1;
            }
            struct rc_function_stats s; require(rc_runtime_function_stats(r,handles[n],&s,NULL));
            compilation += s.compilation_ns; text += s.generated_size;
        }
        start = now();
        for (unsigned round = 0; round < 16; ++round) for (size_t n = 0; n < count; ++n) {
            uint64_t value; require(rc_runtime_call_integer(r,handles[n],NULL,0,&value,NULL)); if (value != n) return 1;
        }
        uint64_t lookup = now()-start; struct rc_runtime_stats stats; require(rc_runtime_get_stats(r,&stats,NULL));
        size_t occupied = text, available = 0, mappings = stats.executable_bytes/page;
#ifdef RECODE_HAS_SLABS
        occupied = stats.occupied_bytes; available = stats.slab_free_bytes; mappings = stats.executable_mappings;
#endif
        long peak = rss(); start = now(); require(rc_runtime_destroy(&r,NULL)); uint64_t cleanup = now()-start;
        printf("%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu,%zu,%zu,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%ld\n",
            count,registered,compilation,lookup,cleanup,text,occupied,available,stats.executable_bytes,
            stats.peak_executable_bytes,mappings,stats.evictions,stats.compilation_failures,peak);
        free(handles);
    }
    return 0;
}
