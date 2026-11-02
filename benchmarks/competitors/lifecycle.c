#define _POSIX_C_SOURCE 200809L
#include "recode_integer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void check(enum rc_status s) { if (s != RC_OK) exit(1); }
static uint64_t now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
int main(void)
{
    struct rc_runtime_config c=rc_runtime_default_config(); c.policy=RC_POLICY_FIXED; c.hot_threshold=2;
    struct rc_runtime *r=NULL; check(rc_runtime_create(&c,&r,NULL));
    uint64_t construction=0, registration=0, execution=0, removal=0;
    for (uint64_t n=0; n<4096; ++n) {
        struct rc_integer_insn insns[]={{.op=RC_INT_ARGUMENT},
            {.op=RC_INT_CONSTANT,.dst=1,.immediate=n},{.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
        uint64_t start=now(); struct rc_builder *b=NULL; struct rc_integer_program *p=NULL;
        check(rc_builder_create(2,1,&b,NULL));
        for (size_t i=0; i<4; ++i) check(rc_builder_append(b,insns[i],NULL));
        check(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b); construction+=now()-start;
        rc_handle h; start=now(); check(rc_runtime_register_integer(r,p,&h,NULL)); registration+=now()-start;
        rc_integer_program_destroy(p); start=now();
        for (unsigned call=0; call<10; ++call) {
            uint64_t a=UINT64_MAX, value;
            check(rc_runtime_call_integer(r,h,&a,1,&value,NULL)); if (value != a+n) return 1;
        }
        execution+=now()-start; start=now(); check(rc_runtime_remove(r,h,NULL)); removal+=now()-start;
    }
    struct rc_runtime_stats s; check(rc_runtime_get_stats(r,&s,NULL));
    if (s.functions != 0 || s.translations != 0 || s.executable_bytes != 0) return 1;
    printf("functions,construct_ns,register_ns,execute_ns,remove_ns,peak_executable_bytes\n4096,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu\n",construction,registration,execution,removal,s.peak_executable_bytes);
    check(rc_runtime_destroy(&r,NULL)); return 0;
}
