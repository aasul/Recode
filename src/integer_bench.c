#define _POSIX_C_SOURCE 200809L
#include "integer.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); }
}
static uint64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t) != 0) exit(1);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static uint64_t arithmetic(uint64_t a, uint64_t b) { return a + 2*b; }
static uint64_t bitwise(uint64_t a, uint64_t b) { return ((a & b) + (a | b)) ^ (a-b); }
static uint64_t rule(uint64_t a, uint64_t b) { return a > b ? a + 2*b : a ^ b; }
static uint64_t shifts(uint64_t a, uint64_t b) { return a == b ? a << (b & 63) : b >> (a & 63); }
static uint64_t branches(uint64_t a, uint64_t b)
{
    uint64_t value = a;
    for (unsigned n = 0; n < 16; ++n) if (value < b) value += b;
    return value;
}
struct workload { const char *name; struct rc_integer_program *p; rc_integer_function reference; };
static struct rc_integer_program *build(const struct rc_integer_insn *insns, size_t count)
{
    struct rc_builder *builder = NULL; struct rc_integer_program *p = NULL;
    require(rc_builder_create(4,2,&builder,NULL));
    for (size_t n = 0; n < count; ++n) require(rc_builder_append(builder,insns[n],NULL));
    require(rc_builder_finalize(builder,&p,NULL)); rc_builder_destroy(builder); return p;
}
static void inputs(size_t n, unsigned distribution, uint64_t *args)
{
    /* Stateless mixed bits avoid a timed PRNG and keep both engines' work equal. */
    uint64_t x = (uint64_t)n * UINT64_C(0x9e3779b97f4a7c15);
    args[0] = distribution == 1 ? 7 : x;
    args[1] = distribution == 1 ? 3 : distribution == 2 ? x : (x >> 17) ^ UINT64_C(0xdeadbeef);
}
static void run(const struct workload *w, unsigned distribution, size_t calls, unsigned mode, unsigned round)
{
    static const char *names[] = {"c","interpreter","kernel","api-interpret","fixed","auto"};
    uint64_t expected = 0, args[2];
    for (size_t n = 0; n < calls; ++n) { inputs(n,distribution,args); expected += w->reference(args[0],args[1]); }
    struct rc_runtime *r = NULL; rc_handle handle = 0; struct rc_code code = {0};
    rc_integer_function native = NULL; struct rc_runtime_stats stats = {0};
    struct rc_function_stats function = {0}; size_t generated = 0;
    uint64_t total_start = now(), registration_ns = 0, compile_ns = 0;
    if (mode == 2) {
        uint64_t start = now(); bool allocated;
        require(rc_integer_emit(w->p,NULL,0,&generated,NULL));
        require(rc_integer_map(w->p,generated,&code,&native,&allocated,NULL));
        compile_ns = now() - start;
    } else if (mode >= 3) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = mode == 3 ? RC_POLICY_INTERPRET : mode == 4 ? RC_POLICY_FIXED : RC_POLICY_ADAPTIVE;
        c.hot_threshold = 32;
        require(rc_runtime_create(&c,&r,NULL));
        uint64_t start = now(); require(rc_runtime_register_integer(r,w->p,&handle,NULL));
        registration_ns = now() - start;
    }
    rc_integer_function volatile direct = w->reference;
    uint64_t checksum = 0, start = now();
    for (size_t n = 0; n < calls; ++n) {
        inputs(n,distribution,args); uint64_t value;
        switch (mode) {
        case 0: value = direct(args[0],args[1]); break;
        case 1: require(rc_integer_interpret(w->p,args,2,0,&value,NULL)); break;
        case 2: value = native(args[0],args[1]); break;
        default: require(rc_runtime_call_integer(r,handle,args,2,&value,NULL)); break;
        }
        checksum += value;
    }
    uint64_t duration = now() - start, total = now() - total_start;
    if (checksum != expected) { fprintf(stderr,"checksum mismatch %s\n",w->name); exit(1); }
    if (r != NULL) {
        require(rc_runtime_get_stats(r,&stats,NULL));
        require(rc_runtime_function_stats(r,handle,&function,NULL));
        compile_ns = function.compilation_ns; generated = function.generated_size;
    }
    start = now();
    if (r != NULL) { require(rc_runtime_clear(r,NULL)); require(rc_runtime_destroy(&r,NULL)); }
    require(rc_code_free(&code,NULL));
    uint64_t cleanup = now() - start;
    printf("integer,%s,distribution,%u,mode,%s,calls,%zu,round,%u,ns,%.3f,total_us,%.3f,register_ns,%" PRIu64
           ",compile_ns,%" PRIu64 ",cleanup_ns,%" PRIu64 ",generated,%zu,interpreted,%" PRIu64 ",jitted,%" PRIu64
           ",hits,%" PRIu64 ",misses,%" PRIu64 ",peak,%zu,metadata,%zu,program,%zu,checksum,%" PRIu64 "\n",
           w->name,distribution,names[mode],calls,round,(double)duration/(double)calls,(double)total/1000.0,
           registration_ns,compile_ns,cleanup,generated,function.interpreted,function.jitted,stats.cache_hits,
           stats.cache_misses,stats.peak_executable_bytes,stats.metadata_bytes,stats.program_bytes,checksum);
}
static void pressure(void)
{
    const size_t counts[] = {1,32,1024,4096};
    long page = sysconf(_SC_PAGESIZE); if (page <= 0) exit(1);
    for (size_t c = 0; c < 4; ++c) {
        struct rc_runtime_config config = rc_runtime_default_config();
        config.policy = RC_POLICY_FIXED; config.hot_threshold = 2;
        config.executable_budget = (size_t)page * 16;
        struct rc_runtime *r = NULL; require(rc_runtime_create(&config,&r,NULL));
        uint64_t start = now(), registration = 0;
        for (size_t n = 0; n < counts[c]; ++n) {
            struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},
                {.op=RC_INT_CONSTANT,.dst=1,.immediate=n},{.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
            struct rc_integer_program *p = build(insns,4); rc_handle h;
            uint64_t before = now(); require(rc_runtime_register_integer(r,p,&h,NULL)); registration += now()-before;
            rc_integer_program_destroy(p);
            uint64_t args[] = {3,7}, value;
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                require(rc_runtime_call_integer(r,h,args,2,&value,NULL)); if (value != 3+n) exit(1);
            }
        }
        uint64_t duration = now()-start; struct rc_runtime_stats stats;
        require(rc_runtime_get_stats(r,&stats,NULL));
        if (stats.executable_bytes > config.executable_budget || stats.peak_executable_bytes > config.executable_budget) exit(1);
        start = now(); require(rc_runtime_clear(r,NULL));
        struct rc_runtime_stats cleared; require(rc_runtime_get_stats(r,&cleared,NULL));
        if (cleared.executable_bytes != 0) exit(1);
        require(rc_runtime_destroy(&r,NULL));
        printf("integer-pressure,functions,%zu,total_us,%.3f,register_us,%.3f,cleanup_us,%.3f,hits,%" PRIu64
               ",misses,%" PRIu64 ",evictions,%" PRIu64 ",peak,%zu,peak_pages,%zu,metadata,%zu,program,%zu\n",
               counts[c],(double)duration/1000.0,(double)registration/1000.0,(double)(now()-start)/1000.0,
               stats.cache_hits,stats.cache_misses,stats.evictions,stats.peak_executable_bytes,
               stats.peak_executable_bytes/(size_t)page,stats.metadata_bytes,stats.program_bytes);
    }
}
int main(int argc, char **argv)
{
    printf("compiler,%s,clock,CLOCK_MONOTONIC,rounds,5,page,%ld\n",__VERSION__,sysconf(_SC_PAGESIZE));
    if (argc == 2 && strcmp(argv[1],"--pressure") == 0) { pressure(); return 0; }
    if (argc != 1) return 1;
    const struct rc_integer_insn arithmetic_ir[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_CONSTANT,.dst=2,.immediate=2},
        {.op=RC_INT_MUL,.dst=2,.left=1,.right=2},{.op=RC_INT_ADD,.dst=2,.right=2},{.op=RC_INT_RETURN,.left=2}};
    const struct rc_integer_insn bitwise_ir[] = {{.op=RC_INT_ARGUMENT},{.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_AND,.dst=2,.right=1},{.op=RC_INT_OR,.dst=3,.right=1},{.op=RC_INT_ADD,.dst=2,.left=2,.right=3},
        {.op=RC_INT_SUB,.dst=3,.right=1},{.op=RC_INT_XOR,.dst=2,.left=2,.right=3},{.op=RC_INT_RETURN,.left=2}};
    const struct rc_integer_insn rule_ir[] = {{.op=RC_INT_ARGUMENT},{.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_ULT,.dst=2,.left=1},{.op=RC_INT_BRANCH,.left=2,.target=6},{.op=RC_INT_XOR,.dst=3,.right=1},
        {.op=RC_INT_RETURN,.left=3},{.op=RC_INT_CONSTANT,.dst=2,.immediate=2},{.op=RC_INT_MUL,.dst=2,.left=2,.right=1},
        {.op=RC_INT_ADD,.dst=3,.right=2},{.op=RC_INT_RETURN,.left=3}};
    const struct rc_integer_insn shift_ir[] = {{.op=RC_INT_ARGUMENT},{.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_EQ,.dst=2,.right=1},{.op=RC_INT_BRANCH,.left=2,.target=6},
        {.op=RC_INT_SHR,.dst=3,.left=1},{.op=RC_INT_RETURN,.left=3},
        {.op=RC_INT_SHL,.dst=3,.right=1},{.op=RC_INT_RETURN,.left=3}};
    struct rc_integer_insn branch_ir[51] = {{.op=RC_INT_ARGUMENT},{.op=RC_INT_ARGUMENT,.dst=1,.immediate=1}};
    for (uint32_t n = 0; n < 16; ++n) {
        uint32_t pc = 2 + 3*n;
        branch_ir[pc] = (struct rc_integer_insn){.op=RC_INT_ULE,.dst=2,.left=1};
        branch_ir[pc+1] = (struct rc_integer_insn){.op=RC_INT_BRANCH,.left=2,.target=pc+3};
        branch_ir[pc+2] = (struct rc_integer_insn){.op=RC_INT_ADD,.right=1};
    }
    branch_ir[50] = (struct rc_integer_insn){.op=RC_INT_RETURN};
    struct workload workloads[] = {{"arithmetic",build(arithmetic_ir,6),arithmetic},
        {"bitwise",build(bitwise_ir,8),bitwise},{"rule",build(rule_ir,10),rule},
        {"shifts",build(shift_ir,8),shifts},{"branches",build(branch_ir,51),branches}};
    const size_t counts[] = {1,32,50000};
    for (size_t w = 0; w < 5; ++w) {
        uint64_t start = now();
        for (unsigned n = 0; n < 100; ++n) {
            struct rc_integer_program *p = build(workloads[w].p->insns,workloads[w].p->count);
            rc_integer_program_destroy(p);
        }
        printf("construct,%s,mean_ns,%.3f\n",workloads[w].name,(double)(now()-start)/100.0);
        for (unsigned distribution = 0; distribution < 3; ++distribution)
            for (size_t c = 0; c < 3; ++c)
                for (unsigned round = 0; round < 5; ++round)
                    for (unsigned offset = 0; offset < 6; ++offset)
                        run(&workloads[w],distribution,counts[c],(round+offset)%6,round+1);
        rc_integer_program_destroy(workloads[w].p);
    }
    return 0;
}
