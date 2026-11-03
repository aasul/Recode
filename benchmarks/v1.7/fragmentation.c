#define _POSIX_C_SOURCE 200809L
#include "recode_integer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static void require(enum rc_status s) { if (s != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(s)); exit(1); } }
static uint64_t now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static rc_handle register_value(struct rc_runtime *r, uint64_t value)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL; rc_handle h;
    require(rc_builder_create(1,0,&b,NULL));
    require(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_CONSTANT,.immediate=value},NULL));
    require(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL));
    require(rc_builder_finalize(b,&p,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
    rc_integer_program_destroy(p); rc_builder_destroy(b);
    for (unsigned n = 0; n < 2; ++n) { uint64_t out; require(rc_runtime_call_integer(r,h,NULL,0,&out,NULL)); if (out != value) exit(1); }
    return h;
}
static void row(struct rc_runtime *r, const char *phase, uint64_t duration)
{
    struct rc_runtime_stats s; require(rc_runtime_get_stats(r,&s,NULL));
    int64_t occupied = -1, available = -1;
#ifdef RECODE_HAS_SLABS
    occupied = (int64_t)s.occupied_bytes; available = (int64_t)s.slab_free_bytes;
#endif
    printf("%s,%zu,%zu,%" PRId64 ",%" PRId64 ",%" PRIu64 "\n",phase,s.functions,s.executable_bytes,occupied,available,duration);
}
int main(void)
{
    enum { COUNT = 1024 }; rc_handle handles[COUNT];
    struct rc_runtime_config c = rc_runtime_default_config(); c.policy = RC_POLICY_FIXED; c.hot_threshold = 2;
    struct rc_runtime *r = NULL; require(rc_runtime_create(&c,&r,NULL));
    puts("phase,handles,committed_bytes,occupied_bytes,slab_free_bytes,total_ns");
    uint64_t start = now();
    for (size_t n = 0; n < COUNT; ++n) handles[n] = register_value(r,n);
    row(r,"full",now()-start); start = now();
    for (size_t n = 0; n < COUNT; n += 2) require(rc_runtime_remove(r,handles[n],NULL));
    row(r,"half",now()-start); start = now();
    for (size_t n = 0; n < COUNT; n += 2) handles[n] = register_value(r,COUNT+n);
    row(r,"churn",now()-start); start = now(); require(rc_runtime_clear(r,NULL));
    row(r,"clear",now()-start); require(rc_runtime_destroy(&r,NULL));
    return 0;
}
