#define _POSIX_C_SOURCE 200809L
#include "bench.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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
int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr,"usage: compare mode workload[0..5] calls threads\n"); return 1; }
    /* Accept the zero-based workload separately from positive counts. */
    unsigned kind=0;
    if (argv[2][0] != '0' || argv[2][1] != '\0') kind=(unsigned)number(argv[2],5);
    size_t calls=number(argv[3],10000000), threads=number(argv[4],16);
    struct metrics m={0}; uint64_t total=bench_now();
    struct engine *e=engine_open(argv[1],&m); struct workload w=workload(kind);
    engine_build(e,&w,&m);
    struct input first; inputs(0,&first); uint64_t start=bench_now();
    if (engine_call(e,&first) != reference(kind,&first)) return 1;
    m.first_ns=bench_now()-start;
    /* Cold phase includes every call and policy-driven compilation. */
    struct worker cold={e,calls,0}; start=bench_now(); execute(&cold);
    uint64_t cold_ns=bench_now()-start, end_to_end=bench_now()-total;
    uint64_t expected=0;
    for (size_t n=0; n<calls; ++n) { struct input in; inputs(n,&in); expected+=reference(kind,&in); }
    if (cold.sum != expected) return 1;
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
    engine_stats(e,&m); start=bench_now(); engine_close(e); uint64_t cleanup=bench_now()-start;
    struct rusage usage; if (getrusage(RUSAGE_SELF,&usage) != 0) return 1;
    /* ru_maxrss can retain the forking launcher's pre-exec high-water mark. */
    FILE *status=fopen("/proc/self/status","r"); if (status == NULL) return 1;
    char line[256]; long rss=-1;
    while (fgets(line,sizeof(line),status) != NULL)
        if (sscanf(line,"VmHWM: %ld kB",&rss) == 1) break;
    if (fclose(status) != 0 || rss<0) return 1;
    puts("engine,workload,calls,threads,init_ns,build_ns,validate_ns,register_ns,compile_ns,first_ns,cold_ns,warm_ns,end_to_end_ns,cleanup_ns,code_bytes,executable_bytes,peak_rss_kib,rusage_peak_kib,interpreted,jitted,evictions,checksum");
    printf("%s,%s,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRId64 ",%" PRId64 ",%ld,%ld,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
           argv[1],w.name,calls,threads,m.init_ns,m.build_ns,m.validate_ns,m.register_ns,m.compile_ns,m.first_ns,
           cold_ns,warm_ns,end_to_end,cleanup,m.code_bytes,m.executable_bytes,rss,usage.ru_maxrss,m.interpreted,m.jitted,m.evictions,expected);
    return 0;
}
