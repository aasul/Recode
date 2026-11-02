#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

uint64_t bench_now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
struct worker { struct engine *e; size_t calls; uint64_t sum; };
static void *execute(void *context)
{
    struct worker *w=context; uint64_t sum=0;
    for (size_t n=0; n<w->calls; ++n) { struct input in; inputs(n,&in); sum+=engine_call(w->e,&in); }
    w->sum=sum; return NULL;
}
static size_t number(const char *s, size_t limit)
{
    char *end; errno=0; unsigned long long n=strtoull(s,&end,10);
    if (errno != 0 || *s == '-' || *s == '\0' || *end != '\0' || n == 0 || n>limit) exit(1);
    return (size_t)n;
}
static long peak_rss(void)
{
    /* ru_maxrss can retain the forking launcher's pre-exec high-water mark. */
    FILE *status=fopen("/proc/self/status","r"); if (status == NULL) exit(1);
    char line[256]; long rss=-1;
    while (fgets(line,sizeof(line),status) != NULL)
        if (sscanf(line,"VmHWM: %ld kB",&rss) == 1) break;
    if (fclose(status) != 0 || rss<0) exit(1);
    return rss;
}
static int lifecycle(const char *mode)
{
    if (strcmp(mode,"c") == 0) return 2; /* C has no dynamic compilation adapter. */
    struct metrics sum={0}; uint64_t total=bench_now(), cleanup=0, checksum=0;
    bool measured_size=true, measured_executable=true;
    for (unsigned n=0; n<64; ++n) {
        struct metrics m={0}; struct engine *e=engine_open(mode,&m);
        struct workload w=workload(0); w.insns[2].immediate=n+2;
        engine_build(e,&w,&m);
        for (unsigned call=0; call<2; ++call) {
            struct input in; inputs(n+call,&in);
            uint64_t result=engine_call(e,&in);
            if (result != in.args[0]+(uint64_t)(n+2)*in.args[1]) exit(1);
            checksum+=result;
        }
        engine_stats(e,&m);
        if (strcmp(mode,"llvm") == 0 || strcmp(mode,"mir") == 0) m.jitted=2;
        uint64_t start=bench_now(); engine_close(e); cleanup+=bench_now()-start;
        sum.init_ns+=m.init_ns; sum.build_ns+=m.build_ns; sum.validate_ns+=m.validate_ns;
        sum.register_ns+=m.register_ns; sum.compile_ns+=m.compile_ns;
        sum.interpreted+=m.interpreted; sum.jitted+=m.jitted;
        if (m.code_bytes<0) measured_size=false; else sum.code_bytes+=m.code_bytes;
        if (m.executable_bytes<0) measured_executable=false;
        else if (m.executable_bytes>sum.executable_bytes) sum.executable_bytes=m.executable_bytes;
    }
    uint64_t duration=bench_now()-total;
    puts("engine,functions,calls_per_function,init_ns,build_ns,validate_ns,register_ns,compile_ns,cleanup_ns,end_to_end_ns,code_bytes,executable_bytes,peak_rss_kib,interpreted,jitted,checksum");
    printf("%s,64,2,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRId64 ",%" PRId64 ",%ld,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",mode,sum.init_ns,sum.build_ns,sum.validate_ns,
           sum.register_ns,sum.compile_ns,cleanup,duration,measured_size?sum.code_bytes:-1,
           measured_executable?sum.executable_bytes:-1,peak_rss(),sum.interpreted,sum.jitted,checksum);
    return 0;
}
static int bulk(const char *mode, unsigned kind, size_t count)
{
    uint64_t total=bench_now(); struct metrics m={0}; struct engine *e=engine_open(mode,&m);
    struct workload w=workload(kind); engine_build(e,&w,&m);
    uint64_t start=bench_now(); struct input *in=malloc(count*sizeof(*in));
    uint64_t *results=malloc(count*sizeof(*results)); if (in == NULL || results == NULL) exit(1);
    for (size_t n=0; n<count; ++n) inputs(n,&in[n]);
    engine_prepare_bulk(e,in,count); uint64_t prepare_ns=bench_now()-start;
    start=bench_now(); engine_bulk(e,in,count,results);
    uint64_t cold_ns=bench_now()-start, end_to_end=bench_now()-total;
    for (size_t n=0; n<count; ++n) if (results[n] != reference(kind,&in[n])) exit(1);
    size_t warm=0;
    for (; warm<8 || warm*count<128; ++warm) engine_bulk(e,in,count,results);
    size_t repeats=32768/count; if (repeats<8) repeats=8;
    start=bench_now();
    for (size_t repeat=0; repeat<repeats; ++repeat) engine_bulk(e,in,count,results);
    uint64_t warm_ns=bench_now()-start;
    for (size_t n=0; n<count; ++n) if (results[n] != reference(kind,&in[n])) exit(1);
    engine_stats(e,&m);
    if (strcmp(mode,"llvm") == 0 || strcmp(mode,"mir") == 0) m.jitted=count*(1+warm+repeats);
    start=bench_now(); engine_close(e); free(results); free(in);
    uint64_t cleanup_ns=bench_now()-start;
    puts("engine,workload,items,repeats,init_ns,build_ns,validate_ns,register_ns,prepare_ns,compile_ns,cold_ns,end_to_end_ns,warm_ns,warm_ns_per_item,cleanup_ns,code_bytes,executable_bytes,peak_rss_kib,interpreted,jitted");
    printf("%s,%s,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%.3f,%" PRIu64 ",%" PRId64 ",%" PRId64 ",%ld,%" PRIu64 ",%" PRIu64 "\n",
           mode,w.name,count,repeats,m.init_ns,m.build_ns,m.validate_ns,m.register_ns,prepare_ns,m.compile_ns,cold_ns,end_to_end,warm_ns,
           (double)warm_ns/(double)(count*repeats),cleanup_ns,m.code_bytes,m.executable_bytes,peak_rss(),m.interpreted,m.jitted);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1],"--bulk") == 0) {
        unsigned kind=0;
        if (strcmp(argv[3],"0") != 0) kind=(unsigned)number(argv[3],5);
        return bulk(argv[2],kind,number(argv[4],65536));
    }
    if (argc == 3 && strcmp(argv[1],"--lifecycle") == 0) return lifecycle(argv[2]);
    if (argc != 5) { fprintf(stderr,"usage: compare mode workload[0..5] calls threads\n"); return 1; }
    /* Accept the zero-based workload separately from positive counts. */
    unsigned kind=0;
    if (argv[2][0] != '0' || argv[2][1] != '\0') kind=(unsigned)number(argv[2],5);
    size_t calls=number(argv[3],10000000), threads=number(argv[4],16);
    struct metrics m={0}; uint64_t total=bench_now();
    struct engine *e=engine_open(argv[1],&m); struct workload w=workload(kind);
    engine_build(e,&w,&m);
    struct input first; inputs(0,&first); uint64_t start=bench_now();
    uint64_t first_value=engine_call(e,&first);
    m.first_ns=bench_now()-start;
    uint64_t cold_checksum=first_value;
    for (size_t n=1; n<calls; ++n) { struct input in; inputs(n,&in); cold_checksum+=engine_call(e,&in); }
    uint64_t cold_ns=bench_now()-start, end_to_end=bench_now()-total;
    struct metrics cold=m; engine_stats(e,&cold);
    bool external=strcmp(argv[1],"llvm") == 0 || strcmp(argv[1],"mir") == 0;
    if (external) cold.jitted=calls; /* These adapters always invoke native entries. */
    if (strcmp(argv[1],"c") != 0 && cold.interpreted+cold.jitted != calls) return 1;
    if (first_value != reference(kind,&first)) return 1;
    uint64_t expected=0;
    for (size_t n=0; n<calls; ++n) { struct input in; inputs(n,&in); expected+=reference(kind,&in); }
    if (cold_checksum != expected) return 1;
    /* Untimed, per-result differential verification covers boundary bit patterns. */
    for (size_t n=0; n<1024; ++n) {
        struct input in; inputs(n,&in);
        if (n<4) { in.args[0]=n%2 ? UINT64_MAX : UINT64_C(0x8000000000000000); in.args[1]=n; }
        if (engine_call(e,&in) != reference(kind,&in)) return 1;
    }
    pthread_t ids[16]; struct worker workers[16]; start=bench_now();
    for (size_t n=0; n<threads; ++n) {
        workers[n]=(struct worker){e,calls,0};
        if (threads == 1) execute(&workers[n]);
        else if (pthread_create(&ids[n],NULL,execute,&workers[n]) != 0) exit(1);
    }
    if (threads != 1) for (size_t n=0; n<threads; ++n) if (pthread_join(ids[n],NULL) != 0) exit(1);
    uint64_t warm_ns=bench_now()-start;
    for (size_t n=0; n<threads; ++n) if (workers[n].sum != expected) return 1;
    engine_stats(e,&m);
    if (external) m.jitted=calls+1024+calls*threads;
    start=bench_now(); engine_close(e); uint64_t cleanup=bench_now()-start;
    struct rusage usage; if (getrusage(RUSAGE_SELF,&usage) != 0) return 1;
    long rss=peak_rss();
    puts("engine,workload,calls,threads,init_ns,build_ns,validate_ns,register_ns,compile_ns,first_ns,cold_ns,cold_compile_ns,cold_interpreted,cold_jitted,warm_ns,end_to_end_ns,cleanup_ns,code_bytes,executable_bytes,peak_rss_kib,rusage_peak_kib,interpreted,jitted,evictions,checksum");
    printf("%s,%s,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRId64 ",%" PRId64 ",%ld,%ld,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
           argv[1],w.name,calls,threads,m.init_ns,m.build_ns,m.validate_ns,m.register_ns,m.compile_ns,m.first_ns,
           cold_ns,cold.compile_ns,cold.interpreted,cold.jitted,warm_ns,end_to_end,cleanup,m.code_bytes,m.executable_bytes,rss,usage.ru_maxrss,m.interpreted,m.jitted,m.evictions,expected);
    return 0;
}
