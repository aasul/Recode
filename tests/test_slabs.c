#include "bench.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)

static rc_handle register_value(struct rc_runtime *r, uint64_t value)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL; rc_handle h;
    CHECK(rc_builder_create(1,0,&b,NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_CONSTANT,.immediate=value},NULL) == RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK);
    CHECK(rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    rc_integer_program_destroy(p); rc_builder_destroy(b);
    return h;
}
static void check_value(struct rc_runtime *r, rc_handle h, uint64_t value)
{
    uint64_t out;
    CHECK(rc_runtime_call_integer(r,h,NULL,0,&out,NULL) == RC_OK && out == value);
    struct rc_integer_batch_item items[33] = {{0}}; uint64_t results[33]; size_t done;
    CHECK(rc_runtime_call_integer_batch(r,h,items,33,results,33,&done,NULL) == RC_OK && done == 33);
    for (size_t n = 0; n < 33; ++n) CHECK(results[n] == value);
}
static void check_workload(struct rc_runtime *r, rc_handle h, unsigned kind)
{
    struct input in[65]; struct rc_memory_view views[65]; struct rc_integer_batch_item items[65];
    uint64_t results[65]; size_t done;
    for (size_t n = 0; n < 65; ++n) {
        inputs(n,&in[n]); views[n] = (struct rc_memory_view){in[n].data,sizeof(in[n].data)};
        items[n] = (struct rc_integer_batch_item){in[n].args,2,&views[n]};
        CHECK(rc_runtime_call_integer_view(r,h,in[n].args,2,&views[n],results+n,NULL) == RC_OK);
        CHECK(results[n] == reference(kind,&in[n]));
    }
    CHECK(rc_runtime_call_integer_batch(r,h,items,65,results,65,&done,NULL) == RC_OK && done == 65);
    for (size_t n = 0; n < 65; ++n) CHECK(results[n] == reference(kind,&in[n]));
}
static void test_relocation(struct rc_runtime *r)
{
    rc_handle handles[6];
    for (unsigned kind = 0; kind < 6; ++kind) {
        struct workload w = workload(kind); struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
        CHECK(rc_builder_create(8,2,&b,NULL) == RC_OK);
        for (size_t n = 0; n < w.count; ++n) CHECK(rc_builder_append(b,w.insns[n],NULL) == RC_OK);
        CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK);
        CHECK(rc_runtime_register_integer(r,p,handles+kind,NULL) == RC_OK);
        rc_integer_program_destroy(p); rc_builder_destroy(b);
        for (unsigned old = 0; old <= kind; ++old) check_workload(r,handles[old],old);
    }
    for (unsigned kind = 0; kind < 6; ++kind) CHECK(rc_runtime_remove(r,handles[kind],NULL) == RC_OK);
}
struct worker { struct rc_runtime *r; rc_handle *handles; unsigned id; };
static void *execute(void *context)
{
    struct worker *w = context;
    for (unsigned n = 0; n < 400; ++n) check_value(w->r,w->handles[(n+w->id)%64],(n+w->id)%64);
    return NULL;
}
int main(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    struct rc_runtime_config c = rc_runtime_default_config(); c.policy = RC_POLICY_FIXED; c.hot_threshold = 2;
    c.executable_budget = 64*page;
    struct rc_runtime *r = NULL; CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
    test_relocation(r);
    rc_handle handles[256];
    for (size_t n = 0; n < 256; ++n) {
        handles[n] = register_value(r,n); check_value(r,handles[n],n);
        /* Revisit earlier members after each immutable-page replacement. */
        for (size_t old = 0; old <= n; ++old) check_value(r,handles[old],old);
    }
    struct rc_runtime_stats stats; CHECK(rc_runtime_get_stats(r,&stats,NULL) == RC_OK);
    CHECK(stats.executable_bytes < 256*page/4 && stats.generated_bytes <= stats.occupied_bytes);
    CHECK(stats.executable_mappings*page == stats.executable_bytes && stats.retired_executable_bytes == 0);
    CHECK(stats.occupied_bytes+stats.slab_free_bytes <= stats.executable_bytes);
    rc_handle alias = register_value(r,0); check_value(r,alias,0);
    CHECK(rc_runtime_remove(r,alias,NULL) == RC_OK);
    pthread_t threads[4]; struct worker workers[4];
    for (unsigned n = 0; n < 4; ++n) {
        workers[n] = (struct worker){r,handles,n}; CHECK(pthread_create(&threads[n],NULL,execute,&workers[n]) == 0);
    }
    for (unsigned n = 256; n < 512; ++n) {
        rc_handle h = register_value(r,n); check_value(r,h,n); CHECK(rc_runtime_remove(r,h,NULL) == RC_OK);
    }
    for (unsigned n = 0; n < 4; ++n) CHECK(pthread_join(threads[n],NULL) == 0);
    for (size_t n = 0; n < 256; n += 2) CHECK(rc_runtime_remove(r,handles[n],NULL) == RC_OK);
    for (size_t n = 1; n < 256; n += 2) check_value(r,handles[n],n);
    CHECK(rc_runtime_clear(r,NULL) == RC_OK);
    CHECK(rc_runtime_get_stats(r,&stats,NULL) == RC_OK && stats.executable_bytes == 0 && stats.executable_mappings == 0);
    for (size_t n = 1; n < 256; n += 2) CHECK(rc_runtime_remove(r,handles[n],NULL) == RC_OK);
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    puts("shared pages, relocated scalar/batch entries, aliases, removals and concurrent replacement passed");
    return 0;
}
