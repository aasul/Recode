#include "recode_integer.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
struct worker {
    struct rc_runtime *runtime;
    const struct rc_integer_program *program;
    atomic_bool *start;
    unsigned id;
};
static void *work(void *context)
{
    struct worker *w = context;
    while (!atomic_load(w->start)) { }
    for (unsigned round = 0; round < 500; ++round) {
        rc_handle h, alias;
        CHECK(rc_runtime_register_integer(w->runtime,w->program,&h,NULL) == RC_OK);
        CHECK(rc_runtime_register_integer(w->runtime,w->program,&alias,NULL) == RC_OK && h != alias);
        for (unsigned call = 0; call < 8; ++call) {
            uint8_t bytes[] = {(uint8_t)(round+w->id),1,2,3};
            struct rc_memory_view view = {bytes,sizeof(bytes)};
            uint64_t args[] = {call%2 ? 0 : UINT64_MAX,w->id}, result = 91;
            enum rc_status status = rc_runtime_call_integer_view(w->runtime,call%2 ? h : alias,args,2,&view,&result,NULL);
            if (call%2) CHECK(status == RC_OK && result == (uint64_t)bytes[0]+w->id);
            else CHECK(status == RC_BOUNDS && result == 91);
        }
        if (round%3 == 0) {
            enum rc_status status = rc_runtime_invalidate(w->runtime,alias,NULL);
            CHECK(status == RC_OK || status == RC_BUSY);
        }
        CHECK(rc_runtime_remove(w->runtime,h,NULL) == RC_OK);
        CHECK(rc_runtime_remove(w->runtime,alias,NULL) == RC_OK);
    }
    return NULL;
}
int main(void)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    const struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_LOAD_U8},
        {.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    CHECK(rc_builder_create(2,2,&b,NULL) == RC_OK);
    for (size_t n = 0; n < 5; ++n) CHECK(rc_builder_append(b,insns[n],NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK); rc_builder_destroy(b);
    for (unsigned round = 0; round < 4; ++round) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = RC_POLICY_FIXED; c.hot_threshold = 2; c.retry_calls = 1;
        struct rc_runtime *r = NULL; CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
        atomic_bool start = false; struct worker workers[6]; pthread_t threads[6];
        for (unsigned n = 0; n < 6; ++n) {
            workers[n] = (struct worker){r,p,&start,n};
            CHECK(pthread_create(&threads[n],NULL,work,&workers[n]) == 0);
        }
        atomic_store(&start,true);
        for (unsigned n = 0; n < 6; ++n) CHECK(pthread_join(threads[n],NULL) == 0);
        struct rc_runtime_stats stats; CHECK(rc_runtime_get_stats(r,&stats,NULL) == RC_OK);
        CHECK(stats.invocations == 24000 && stats.functions == 0 && stats.translations == 0);
        CHECK(stats.executable_bytes == 0 && stats.program_bytes == 0 && stats.active_calls == 0);
        CHECK(stats.compilations > 0 && stats.release_failures == 0);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK && r == NULL);
    }
    rc_integer_program_destroy(p);
    puts("96,000 calls with concurrent registration, aliases, invalidation, removal and shutdown passed");
    return 0;
}
