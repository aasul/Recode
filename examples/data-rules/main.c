#define _POSIX_C_SOURCE 200809L
#include <recode_integer.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); }
}
/* Record: enabled:u8, quantity:u16, price:u32, credit:u64, little-endian.
 * Return the discounted total if enabled, above the floor and within credit. */
static struct rc_integer_program *build(void)
{
    const struct rc_integer_insn insns[] = {
        {.op=RC_INT_ARGUMENT,.dst=0}, {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_CONSTANT,.dst=2}, {.op=RC_INT_LOAD_U8,.dst=3,.left=2},
        {.op=RC_INT_BRANCH,.left=3,.target=7},
        {.op=RC_INT_CONSTANT,.dst=3}, {.op=RC_INT_RETURN,.left=3},
        {.op=RC_INT_CONSTANT,.dst=2,.immediate=1}, {.op=RC_INT_LOAD_U16,.dst=3,.left=2},
        {.op=RC_INT_CONSTANT,.dst=2,.immediate=3}, {.op=RC_INT_LOAD_U32,.dst=4,.left=2},
        {.op=RC_INT_MUL,.dst=3,.left=3,.right=4}, {.op=RC_INT_SUB,.dst=3,.left=3,.right=1},
        {.op=RC_INT_CONSTANT,.dst=2,.immediate=7}, {.op=RC_INT_LOAD_U64,.dst=4,.left=2},
        {.op=RC_INT_ULE,.dst=4,.left=3,.right=4}, {.op=RC_INT_ULT,.dst=2,.left=0,.right=3},
        {.op=RC_INT_AND,.dst=2,.left=2,.right=4}, {.op=RC_INT_BRANCH,.left=2,.target=21},
        {.op=RC_INT_CONSTANT,.dst=3}, {.op=RC_INT_RETURN,.left=3}, {.op=RC_INT_RETURN,.left=3}
    };
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    require(rc_builder_create(5,2,&b,NULL));
    for (size_t n = 0; n < sizeof(insns)/sizeof(insns[0]); ++n) require(rc_builder_append(b,insns[n],NULL));
    require(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
    return p;
}
static uint64_t little(const uint8_t *bytes, unsigned width)
{
    uint64_t value = 0;
    while (width != 0) value = value*256 + bytes[--width];
    return value;
}
static enum rc_status reference(const uint8_t *data, size_t length, const uint64_t *args, uint64_t *out)
{
    if (length < 1) return RC_BOUNDS;
    if (data[0] == 0) { *out = 0; return RC_OK; }
    if (length < 15) return RC_BOUNDS;
    uint64_t total = little(data+1,2)*little(data+3,4) - args[1];
    *out = total > args[0] && total <= little(data+7,8) ? total : 0;
    return RC_OK;
}
static void inputs(size_t n, unsigned distribution, uint8_t *data, uint64_t *args)
{
    uint64_t x = (uint64_t)n*UINT64_C(0x9e3779b97f4a7c15);
    data[0] = (uint8_t)(distribution == 1 ? 1 : n%3);
    for (unsigned b = 1; b < 15; ++b) data[b] = (uint8_t)(x >> ((b%8)*8));
    if (distribution == 2) memset(data+7,0,8);
    args[0] = n%1000; args[1] = n%100;
}
static void verify(struct rc_integer_program *p)
{
    for (enum rc_policy policy = RC_POLICY_INTERPRET; policy <= RC_POLICY_ADAPTIVE; ++policy) {
        struct rc_runtime_config c = rc_runtime_default_config(); c.policy = policy; c.hot_threshold = 2;
        struct rc_runtime *r = NULL; rc_handle h;
        require(rc_runtime_create(&c,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
        for (size_t n = 0; n < 3000; ++n) {
            uint8_t data[16]; uint64_t args[2], expected = 91, a = 91, b = 91;
            inputs(n,(unsigned)(n%3),data+1,args);
            struct rc_memory_view view = {data+1,n%16};
            enum rc_status status = reference(view.data,view.length,args,&expected);
            if (rc_integer_interpret_view(p,args,2,&view,100,&a,NULL) != status ||
                rc_runtime_call_integer_view(r,h,args,2,&view,&b,NULL) != status || a != expected || b != expected) exit(1);
        }
        struct rc_function_stats stats; require(rc_runtime_function_stats(r,h,&stats,NULL));
        if (policy == RC_POLICY_FIXED && stats.jitted == 0) exit(1);
        struct rc_memory_view invalid = {NULL,1}; uint64_t args[2] = {0}, result = 91;
        if (rc_runtime_call_integer_view(r,h,args,2,&invalid,&result,NULL) != RC_ARGUMENT || result != 91) exit(1);
        require(rc_runtime_remove(r,h,NULL)); require(rc_runtime_destroy(&r,NULL));
    }
    puts("installed data rules: independent C, interpreter, JIT and failure outputs agree");
}
static uint64_t now(void)
{
    struct timespec t; if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static void benchmark(struct rc_integer_program *p)
{
    const size_t counts[] = {1,32,50000};
    for (unsigned distribution = 0; distribution < 3; ++distribution)
        for (size_t count = 0; count < 3; ++count)
            for (unsigned round = 0; round < 5; ++round)
                for (unsigned order = 0; order < 5; ++order) {
                    unsigned mode = (round+order)%5;
                    const char *names[] = {"c","interpreter","api-interpret","fixed","auto"};
                    uint64_t expected = 0;
                    for (size_t n = 0; n < counts[count]; ++n) {
                        uint8_t data[15]; uint64_t args[2], value;
                        inputs(n,distribution,data,args); require(reference(data,15,args,&value)); expected += value;
                    }
                    uint64_t total_start = now(); struct rc_runtime *r = NULL; rc_handle h = 0;
                    if (mode >= 2) {
                        struct rc_runtime_config c = rc_runtime_default_config();
                        c.policy = mode == 2 ? RC_POLICY_INTERPRET : mode == 3 ? RC_POLICY_FIXED : RC_POLICY_ADAPTIVE;
                        require(rc_runtime_create(&c,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
                    }
                    uint64_t start = now(), checksum = 0;
                    enum rc_status (*volatile direct)(const uint8_t *,size_t,const uint64_t *,uint64_t *) = reference;
                    for (size_t n = 0; n < counts[count]; ++n) {
                        uint8_t data[15]; uint64_t args[2], value; inputs(n,distribution,data,args);
                        struct rc_memory_view view = {data,15};
                        if (mode == 0) require(direct(data,15,args,&value));
                        else if (mode == 1) require(rc_integer_interpret_view(p,args,2,&view,0,&value,NULL));
                        else require(rc_runtime_call_integer_view(r,h,args,2,&view,&value,NULL));
                        checksum += value;
                    }
                    uint64_t duration = now()-start, total = now()-total_start;
                    if (checksum != expected) exit(1);
                    struct rc_function_stats stats = {0}; if (r != NULL) require(rc_runtime_function_stats(r,h,&stats,NULL));
                    printf("data,rule,distribution,%u,mode,%s,calls,%zu,round,%u,ns,%.3f,total_us,%.3f,compile_ns,%" PRIu64
                           ",jitted,%" PRIu64 ",checksum,%" PRIu64 "\n",distribution,names[mode],counts[count],round+1,
                           (double)duration/(double)counts[count],(double)total/1000.0,stats.compilation_ns,stats.jitted,checksum);
                    if (r != NULL) require(rc_runtime_destroy(&r,NULL));
                }
}
int main(int argc, char **argv)
{
    struct rc_integer_program *p = build();
    if (argc == 1) verify(p);
    else if (argc == 2 && strcmp(argv[1],"--bench") == 0) benchmark(p);
    else { rc_integer_program_destroy(p); return 1; }
    rc_integer_program_destroy(p); return 0;
}
