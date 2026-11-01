#define _POSIX_C_SOURCE 200809L
#include "recode_integer.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Single-thread diagnostic executable. Wrappers measure the static library's
 * operations; use the unwrapped benchmarks for latency comparisons. */
static uint64_t clocks, locks, allocations;
int __real_clock_gettime(clockid_t id, struct timespec *t);
int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
int __wrap_clock_gettime(clockid_t id, struct timespec *t) { ++clocks; return __real_clock_gettime(id,t); }
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) { ++locks; return __real_pthread_mutex_lock(mutex); }
void *__wrap_malloc(size_t size) { ++allocations; return __real_malloc(size); }
void *__wrap_calloc(size_t count, size_t size) { ++allocations; return __real_calloc(count,size); }
static void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); }
}
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
    rc_integer_program_destroy(p);
    return 0;
}
