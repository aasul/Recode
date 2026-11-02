#define _POSIX_C_SOURCE 200809L
#include "recode_integer.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>

/* Single-thread diagnostic executable. Wrappers measure the static library's
 * operations; use the unwrapped benchmarks for latency comparisons. */
static uint64_t clocks, locks, allocations, mappings, publications, unmaps;
int __real_clock_gettime(clockid_t id, struct timespec *t);
int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
int __wrap_clock_gettime(clockid_t id, struct timespec *t) { ++clocks; return __real_clock_gettime(id,t); }
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) { ++locks; return __real_pthread_mutex_lock(mutex); }
void *__wrap_malloc(size_t size) { ++allocations; return __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { ++allocations; return __real_calloc(count,size); }
void *__real_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset);
int __real_mprotect(void *address, size_t size, int protection);
int __real_munmap(void *address, size_t size);
void *__wrap_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    ++mappings; return __real_mmap(address,size,protection,flags,fd,offset);
}
int __wrap_mprotect(void *address, size_t size, int protection)
{
    ++publications; return __real_mprotect(address,size,protection);
}
int __wrap_munmap(void *address, size_t size)
{
    ++unmaps; return __real_munmap(address,size);
}
static void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); }
}
static void measure_compilation(const struct rc_integer_program *p, bool check)
{
    struct rc_runtime_config c = rc_runtime_default_config(); c.policy = RC_POLICY_FIXED; c.hot_threshold = 2;
    struct rc_runtime *r = NULL; rc_handle h; uint64_t args[2] = {256,7}, value;
    require(rc_runtime_create(&c,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
    require(rc_runtime_call_integer(r,h,args,2,&value,NULL));
    clocks = locks = allocations = mappings = publications = unmaps = 0;
    require(rc_runtime_call_integer(r,h,args,2,&value,NULL));
    if (value != 263 || (check && (allocations != 0 || mappings != 1 || publications != 1 || unmaps != 0))) exit(1);
    printf("compile-probe,clocks,%" PRIu64 ",locks,%" PRIu64 ",allocations,%" PRIu64
           ",mmap,%" PRIu64 ",mprotect,%" PRIu64 ",munmap,%" PRIu64 "\n",
           clocks,locks,allocations,mappings,publications,unmaps);
    require(rc_runtime_destroy(&r,NULL));
}
#ifdef RECODE_BATCH_PROBE
static void measure_batch(const struct rc_integer_program *p, bool check)
{
    const size_t count=10000;
    struct rc_integer_batch_item *items=malloc(count*sizeof(*items));
    uint64_t *results=malloc(count*sizeof(*results)), args[2]={256,7};
    if(items==NULL || results==NULL) exit(1);
    for(size_t n=0;n<count;++n) items[n]=(struct rc_integer_batch_item){args,2,NULL};
    for(enum rc_policy policy=RC_POLICY_INTERPRET;policy<=RC_POLICY_ADAPTIVE;++policy) {
        struct rc_runtime_config c=rc_runtime_default_config(); c.policy=policy; c.hot_threshold=2;
        struct rc_runtime *r=NULL; rc_handle h; size_t completed;
        require(rc_runtime_create(&c,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
        for(unsigned warm=0;warm<2;++warm)
            require(rc_runtime_call_integer_batch(r,h,items,128,results,128,&completed,NULL));
        clocks=0; locks=0; allocations=0;
        require(rc_runtime_call_integer_batch(r,h,items,count,results,count,&completed,NULL));
        if(completed!=count) exit(1);
        if(check && policy!=RC_POLICY_ADAPTIVE && (clocks!=1250 || locks!=2 || allocations!=0)) {
            fprintf(stderr,"batch dispatch overhead regression\n"); exit(1);
        }
        printf("batch-probe,policy,%d,items,%zu,clocks,%" PRIu64 ",locks,%" PRIu64 ",allocations,%" PRIu64 "\n",
               (int)policy,count,clocks,locks,allocations);
        for(size_t n=0;n<count;++n) if(results[n]!=263) exit(1);
        require(rc_runtime_destroy(&r,NULL));
    }
    free(results); free(items);
}
#endif

int main(int argc, char **argv)
{
    bool check = argc == 1;
    if (!check && (argc != 2 || strcmp(argv[1],"--measure") != 0)) return 1;
    const uint8_t loop[] = {0x48,0x89,0xf8,0x48,0x83,0xe8,1,0x75,0xfa,0xc3};
    const struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    struct rc_builder *builder = NULL; struct rc_integer_program *p = NULL;
    require(rc_builder_create(2,2,&builder,NULL));
    for (size_t n = 0; n < 4; ++n) require(rc_builder_append(builder,insns[n],NULL));
    require(rc_builder_finalize(builder,&p,NULL)); rc_builder_destroy(builder);
    measure_compilation(p,check);
    for (unsigned integer = 0; integer < 2; ++integer)
        for (enum rc_policy policy = RC_POLICY_INTERPRET; policy <= RC_POLICY_TRACE; ++policy) {
            if (integer && policy == RC_POLICY_TRACE) continue;
            struct rc_runtime_config config = rc_runtime_default_config();
            config.policy = policy; config.hot_threshold = 2; config.trace_hot_threshold = 2;
            struct rc_runtime *r = NULL; rc_handle h;
            require(rc_runtime_create(&config,&r,NULL));
            if (integer) require(rc_runtime_register_integer(r,p,&h,NULL));
            else require(rc_runtime_register(r,loop,sizeof(loop),&h,NULL));
            for (unsigned phase = 0; phase < 2; ++phase) {
                unsigned calls = phase == 0 ? 128 : 10000;
                clocks = 0; locks = 0; allocations = 0;
                for (unsigned n = 0; n < calls; ++n) {
                    uint64_t args[] = {256,7}, value;
                    if (integer) require(rc_runtime_call_integer(r,h,args,2,&value,NULL));
                    else require(rc_runtime_call(r,h,args[0],&value,NULL));
                    if (value != (integer ? 263U : 0U)) exit(1);
                }
                if (check && phase == 1 && policy != RC_POLICY_ADAPTIVE &&
                    (clocks != 1250 || locks != 20000 || allocations != 0)) {
                    fprintf(stderr,"steady dispatch overhead regression\n");
                    exit(1);
                }
                if (phase == 1)
                    printf("probe,integer,%u,policy,%d,calls,%u,clocks,%" PRIu64 ",locks,%" PRIu64 ",allocations,%" PRIu64 "\n",
                           integer,(int)policy,calls,clocks,locks,allocations);
            }
            require(rc_runtime_destroy(&r,NULL));
        }
#ifdef RECODE_BATCH_PROBE
    measure_batch(p,check);
#endif
    rc_integer_program_destroy(p);
    return 0;
}
