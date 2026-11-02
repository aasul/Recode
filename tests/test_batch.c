#define _GNU_SOURCE
#include "recode_integer.h"
#include "internal.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)

static struct rc_integer_program *program(bool memory, uint64_t salt)
{
    struct rc_integer_insn insns[]={{.op=RC_INT_ARGUMENT},
        {.op=memory ? RC_INT_LOAD_U64 : RC_INT_ARGUMENT,.dst=1,.immediate=memory ? 0 : 1},
        {.op=RC_INT_CONSTANT,.dst=2,.immediate=salt},{.op=RC_INT_XOR,.dst=1,.left=1,.right=2},
        {.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    struct rc_builder *b=NULL; struct rc_integer_program *p=NULL;
    CHECK(rc_builder_create(3,2,&b,NULL)==RC_OK);
    for(size_t n=0;n<6;++n) CHECK(rc_builder_append(b,insns[n],NULL)==RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL)==RC_OK); rc_builder_destroy(b); return p;
}
static struct rc_runtime *runtime(enum rc_policy policy, size_t budget)
{
    struct rc_runtime_config c=rc_runtime_default_config(); c.policy=policy;
    c.hot_threshold=2; c.executable_budget=budget;
    c.initial_native_ns=1; c.initial_compile_ns=1; c.sample_period=16;
    struct rc_runtime *r=NULL; CHECK(rc_runtime_create(&c,&r,NULL)==RC_OK); return r;
}
static void test_inputs(void)
{
    struct rc_integer_program *p=program(false,0); struct rc_runtime *r=runtime(RC_POLICY_FIXED,4096);
    rc_handle h; CHECK(rc_runtime_register_integer(r,p,&h,NULL)==RC_OK);
    uint64_t args[2]={UINT64_MAX,9}, out[3]={91,92,93}; size_t done=99; struct rc_error error;
    struct rc_integer_batch_item items[3]={{args,2,NULL},{args,2,NULL},{args,2,NULL}};
    CHECK(rc_runtime_call_integer_batch(r,h,NULL,0,NULL,0,&done,&error)==RC_OK && done==0 && error.status==RC_OK);
    CHECK(rc_runtime_call_integer_batch(r,0,NULL,0,NULL,0,&done,&error)==RC_NOT_FOUND && done==0);
    CHECK(rc_runtime_call_integer_batch(NULL,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,NULL,NULL)==RC_ARGUMENT);
    CHECK(rc_runtime_call_integer_batch(r,h,NULL,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,NULL,3,&done,NULL)==RC_ARGUMENT && done==0);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,2,&done,NULL)==RC_CAPACITY && done==0);
    CHECK(rc_runtime_call_integer_batch(r,h,items,SIZE_MAX,out,SIZE_MAX,&done,NULL)==RC_ARGUMENT && done==0);
    done=99;
    CHECK(rc_runtime_call_integer_batch(r,h,NULL,3,NULL,3,&done,NULL)==RC_ARGUMENT && done==0);
    struct rc_memory_view wrapped={(const void *)(uintptr_t)(UINTPTR_MAX-3),8}; items[0].view=&wrapped;
    done=99; CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
    items[0].view=NULL;
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,&error)==RC_OK && done==3);
    for(size_t n=0;n<3;++n) CHECK(out[n]==8);
    struct rc_function_stats before,after; CHECK(rc_runtime_function_stats(r,h,&before,NULL)==RC_OK);
    CHECK(before.invocations==3 && before.interpreted==3 && before.jitted==0 && before.cache_misses==1);
    for(size_t bad=0;bad<3;++bad) {
        for(size_t n=0;n<3;++n) out[n]=91+n;
        items[bad].argument_count=1;
        CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
        items[bad].argument_count=3;
        CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
        items[bad].argument_count=2; items[bad].arguments=NULL;
        CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
        items[bad].arguments=args;
        struct rc_memory_view invalid={NULL,1}; items[bad].view=&invalid;
        CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==0);
        items[bad].view=NULL;
        for(size_t n=0;n<3;++n) CHECK(out[n]==91+n);
    }
    CHECK(rc_runtime_function_stats(r,h,&after,NULL)==RC_OK && after.invocations==before.invocations);
    /* Output aliasing is rejected without touching even completion/error storage. */
    done=99; items[1].arguments=out;
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==99);
    items[1].arguments=args;
    struct rc_memory_view alias={out,sizeof(out)}; items[1].view=&alias;
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_ARGUMENT && done==99);
    items[1].view=NULL;
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,(uint64_t *)items,3,&done,NULL)==RC_ARGUMENT && done==99);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,(size_t *)out,NULL)==RC_ARGUMENT && out[0]==91);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,(struct rc_error *)out)==RC_ARGUMENT && done==99);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_OK && done==3);
    CHECK(rc_runtime_function_stats(r,h,&after,NULL)==RC_OK && after.jitted==3 && after.compilations==1);
    rc_handle alias_handle; CHECK(rc_runtime_register_integer(r,p,&alias_handle,NULL)==RC_OK);
    CHECK(rc_runtime_call_integer_batch(r,alias_handle,items,3,out,3,&done,NULL)==RC_OK && done==3);
    CHECK(rc_runtime_function_stats(r,alias_handle,&after,NULL)==RC_OK && after.interpreted==3 && after.jitted==0);
    CHECK(rc_runtime_call_integer_batch(r,alias_handle,items,3,out,3,&done,NULL)==RC_OK && done==3);
    CHECK(rc_runtime_function_stats(r,alias_handle,&after,NULL)==RC_OK && after.jitted==3 && after.compilation_attempts==0);
    CHECK(rc_runtime_remove(r,alias_handle,NULL)==RC_OK);
    const uint8_t byte_program[]={0x48,0x89,0xf8,0xc3}; rc_handle byte;
    CHECK(rc_runtime_register(r,byte_program,sizeof(byte_program),&byte,NULL)==RC_OK);
    CHECK(rc_runtime_call_integer_batch(r,byte,NULL,0,NULL,0,&done,NULL)==RC_ARGUMENT && done==0);
    CHECK(rc_runtime_remove(r,h,NULL)==RC_OK);
    CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,NULL)==RC_NOT_FOUND && done==0);
    CHECK(rc_runtime_destroy(&r,NULL)==RC_OK); rc_integer_program_destroy(p);
}
static void test_results(void)
{
    enum { COUNT=4096 };
    struct rc_integer_batch_item *items=calloc(COUNT,sizeof(*items));
    uint64_t (*args)[2]=calloc(COUNT,sizeof(*args)), *out=malloc(COUNT*sizeof(*out));
    CHECK(items!=NULL && args!=NULL && out!=NULL);
    for(size_t n=0;n<COUNT;++n) {
        args[n][0]=(uint64_t)n*UINT64_C(0x9e3779b97f4a7c15); args[n][1]=UINT64_MAX-n;
        items[n]=(struct rc_integer_batch_item){args[n],2,NULL};
    }
    for(enum rc_policy policy=RC_POLICY_INTERPRET;policy<=RC_POLICY_ADAPTIVE;++policy) {
        struct rc_integer_program *p=program(false,123); struct rc_runtime *r=runtime(policy,4096); rc_handle h;
        CHECK(rc_runtime_register_integer(r,p,&h,NULL)==RC_OK); rc_integer_program_destroy(p);
        for(unsigned repeat=0;repeat<4;++repeat) {
            size_t done=99; CHECK(rc_runtime_call_integer_batch(r,h,items,COUNT,out,COUNT,&done,NULL)==RC_OK && done==COUNT);
            for(size_t n=0;n<COUNT;++n) {
                uint64_t scalar; CHECK(out[n]==args[n][0]+(args[n][1]^123));
                CHECK(rc_runtime_call_integer(r,h,args[n],2,&scalar,NULL)==RC_OK && scalar==out[n]);
            }
        }
        struct rc_function_stats stats; CHECK(rc_runtime_function_stats(r,h,&stats,NULL)==RC_OK);
        CHECK(stats.invocations==COUNT*8 && stats.interpreted+stats.jitted==stats.invocations);
        if(policy==RC_POLICY_INTERPRET) CHECK(stats.jitted==0 && stats.compilation_attempts==0);
        if(policy==RC_POLICY_FIXED) CHECK(stats.jitted!=0);
        CHECK(rc_runtime_clear(r,NULL)==RC_OK); CHECK(rc_runtime_destroy(&r,NULL)==RC_OK);
    }
    free(out); free(args); free(items);
}
static void test_bounds(void)
{
    uint8_t data[9]={0,1,2,3,4,5,6,7,8};
    struct rc_memory_view view={data+1,8}; uint64_t args[3][2]={{0,0},{0,0},{0,0}};
    struct rc_integer_batch_item items[3]={{args[0],2,&view},{args[1],2,&view},{args[2],2,&view}};
    for(enum rc_policy policy=RC_POLICY_INTERPRET;policy<=RC_POLICY_ADAPTIVE;++policy) {
        for(size_t budget=0;budget<=4096;budget+=4096) {
            struct rc_integer_program *p=program(true,9); struct rc_runtime *r=runtime(policy,budget); rc_handle h;
            CHECK(rc_runtime_register_integer(r,p,&h,NULL)==RC_OK); rc_integer_program_destroy(p);
            for(unsigned repeat=0;repeat<4;++repeat) for(size_t fail=0;fail<3;++fail) {
                uint64_t out[3]={91,92,93}; size_t done=99; struct rc_error error;
                args[fail][0]=repeat%2 ? UINT64_MAX : 1;
                CHECK(rc_runtime_call_integer_batch(r,h,items,3,out,3,&done,&error)==RC_BOUNDS);
                CHECK(done==fail && error.status==RC_BOUNDS && error.offset==1 && error.system_errno==0);
                for(size_t n=0;n<3;++n) CHECK(out[n]==(n<fail ? (UINT64_C(0x0807060504030201)^9) : 91+n));
                args[fail][0]=0;
            }
            struct rc_function_stats stats; CHECK(rc_runtime_function_stats(r,h,&stats,NULL)==RC_OK);
            CHECK(stats.invocations==24 && stats.interpreted+stats.jitted==24);
            if(budget==0) CHECK(stats.jitted==0);
            if(budget!=0 && policy==RC_POLICY_FIXED) CHECK(stats.jitted!=0);
            CHECK(rc_runtime_destroy(&r,NULL)==RC_OK);
        }
    }
    /* Zero arguments, NULL argument/view pointers, empty views and one item. */
    struct rc_builder *b=NULL; struct rc_integer_program *p=NULL;
    CHECK(rc_builder_create(1,0,&b,NULL)==RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_CONSTANT,.immediate=77},NULL)==RC_OK);
    CHECK(rc_builder_append(b,(struct rc_integer_insn){.op=RC_INT_RETURN},NULL)==RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL)==RC_OK); rc_builder_destroy(b);
    struct rc_runtime *r=runtime(RC_POLICY_FIXED,4096); rc_handle h;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL)==RC_OK); rc_integer_program_destroy(p);
    struct rc_integer_batch_item one={0}; size_t done; uint64_t value;
    for(unsigned n=0;n<3;++n) CHECK(rc_runtime_call_integer_batch(r,h,&one,1,&value,1,&done,NULL)==RC_OK && done==1 && value==77);
    CHECK(rc_runtime_destroy(&r,NULL)==RC_OK);
}
static void test_guard_page(void)
{
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mapping=mmap(NULL,2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(mapping!=MAP_FAILED && mprotect(mapping,page,PROT_READ|PROT_WRITE)==0);
    uint8_t *data=mapping+page-8; for(size_t n=0;n<8;++n) data[n]=(uint8_t)(n+1);
    struct rc_memory_view view={data,8}; uint64_t good[2]={0,0}, bad[2]={1,0};
    struct rc_integer_batch_item items[2]={{good,2,&view},{bad,2,&view}};
    struct rc_integer_program *p=program(true,0);
    for(enum rc_policy policy=RC_POLICY_INTERPRET;policy<=RC_POLICY_FIXED;++policy) {
        struct rc_runtime *r=runtime(policy,4096); rc_handle h; CHECK(rc_runtime_register_integer(r,p,&h,NULL)==RC_OK);
        for(unsigned repeat=0;repeat<3;++repeat) {
            uint64_t out[2]={91,92}; size_t done; struct rc_error error;
            CHECK(rc_runtime_call_integer_batch(r,h,items,2,out,2,&done,&error)==RC_BOUNDS);
            CHECK(done==1 && out[0]==UINT64_C(0x0807060504030201) && out[1]==92 && error.offset==1);
        }
        CHECK(rc_runtime_destroy(&r,NULL)==RC_OK);
    }
    rc_integer_program_destroy(p); CHECK(munmap(mapping,2*page)==0);
}

static void test_sampling(void)
{
    const uint64_t bases[] = {0,1,7,8,15,16,17,UINT64_MAX-100,UINT64_MAX-1,UINT64_MAX};
    const uint64_t periods[] = {1,2,3,7,16,17,UINT64_MAX};
    for (size_t b = 0; b < sizeof(bases)/sizeof(*bases); ++b)
        for (size_t p = 0; p < sizeof(periods)/sizeof(*periods); ++p)
            for (uint64_t samples = 0; samples < 10; ++samples) {
                size_t expected = 0;
                while (expected < 256 && samples >= 8 && rc_saturating_add(bases[b],expected)%periods[p] != 0)
                    ++expected;
                CHECK(rc_unsampled_run(bases[b],samples,256,periods[p]) == expected);
            }
    enum { COUNT=257 };
    uint8_t data[8] = {0}; struct rc_memory_view view = {data,sizeof(data)};
    uint64_t args[COUNT][2] = {{0}}, out[COUNT]; struct rc_integer_batch_item items[COUNT];
    for (size_t n = 0; n < COUNT; ++n) items[n] = (struct rc_integer_batch_item){args[n],2,&view};
    const size_t faults[] = {0,7,8,15,16,17,127,128,255,256};
    for (size_t p = 0; p < sizeof(periods)/sizeof(*periods); ++p) {
        struct rc_runtime_config c = rc_runtime_default_config();
        c.policy = RC_POLICY_FIXED; c.hot_threshold = 2; c.sample_period = periods[p];
        struct rc_runtime *r = NULL; rc_handle h; size_t done;
        struct rc_integer_program *programmed = program(true,0);
        CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK);
        CHECK(rc_runtime_register_integer(r,programmed,&h,NULL) == RC_OK); rc_integer_program_destroy(programmed);
        for (unsigned warm = 0; warm < 2; ++warm)
            CHECK(rc_runtime_call_integer_batch(r,h,items,COUNT,out,COUNT,&done,NULL) == RC_OK && done == COUNT);
        for (size_t f = 0; f < sizeof(faults)/sizeof(*faults); ++f) {
            size_t fault = faults[f]; args[fault][0] = 1;
            for (size_t n = 0; n < COUNT; ++n) out[n] = 91;
            struct rc_function_stats before,after; struct rc_error error;
            CHECK(rc_runtime_function_stats(r,h,&before,NULL) == RC_OK);
            CHECK(rc_runtime_call_integer_batch(r,h,items,COUNT,out,COUNT,&done,&error) == RC_BOUNDS);
            CHECK(done == fault && error.offset == 1 && error.system_errno == 0);
            uint64_t samples = 0;
            for (size_t n = 0; n <= fault; ++n)
                if (before.compiled_samples+samples < 8 || (before.invocations+1+n)%periods[p] == 0) ++samples;
            CHECK(rc_runtime_function_stats(r,h,&after,NULL) == RC_OK);
            CHECK(after.invocations-before.invocations == fault+1 && after.jitted-before.jitted == fault+1);
            CHECK(after.compiled_samples-before.compiled_samples == samples);
            for (size_t n = 0; n < COUNT; ++n) CHECK(out[n] == (n < fault ? 0u : 91u));
            args[fault][0] = 0;
        }
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK);
    }
}

struct worker { struct rc_runtime *r; rc_handle h; unsigned id; bool memory; atomic_bool *start; };
static void *work(void *context)
{
    struct worker *w=context; while(!atomic_load(w->start)) { }
    uint8_t data[8]; memset(data,(int)(w->id+1),sizeof(data));
    struct rc_memory_view view={data,sizeof(data)};
    uint64_t args[128][2], out[128]; struct rc_integer_batch_item items[128];
    for(size_t n=0;n<128;++n) {
        args[n][0]=w->memory ? 0 : n+w->id; args[n][1]=UINT64_MAX-n;
        items[n]=(struct rc_integer_batch_item){args[n],2,w->memory ? &view : NULL};
    }
    for(unsigned repeat=0;repeat<200;++repeat) {
        size_t done; CHECK(rc_runtime_call_integer_batch(w->r,w->h,items,128,out,128,&done,NULL)==RC_OK && done==128);
        for(size_t n=0;n<128;++n) CHECK(out[n]==(w->memory ? UINT64_C(0x0101010101010101)*data[0] : args[n][0]+args[n][1]));
    }
    return NULL;
}
static void test_concurrency(bool memory)
{
    struct rc_integer_program *p=program(memory,0); struct rc_runtime *r=runtime(RC_POLICY_FIXED,4096); rc_handle h,alias;
    CHECK(rc_runtime_register_integer(r,p,&h,NULL)==RC_OK); CHECK(rc_runtime_register_integer(r,p,&alias,NULL)==RC_OK);
    rc_integer_program_destroy(p); pthread_t threads[4]; struct worker workers[4]; atomic_bool start=false;
    for(unsigned n=0;n<4;++n) {
        workers[n]=(struct worker){r,n%2 ? h : alias,n,memory,&start}; CHECK(pthread_create(&threads[n],NULL,work,&workers[n])==0);
    }
    atomic_store(&start,true);
    for(unsigned n=0;n<200;++n) {
        p=program(false,n+1); rc_handle other; CHECK(rc_runtime_register_integer(r,p,&other,NULL)==RC_OK);
        rc_integer_program_destroy(p); uint64_t args[2]={1,2},out[2];
        struct rc_integer_batch_item items[2]={{args,2,NULL},{args,2,NULL}}; size_t done;
        for(unsigned repeat=0;repeat<2;++repeat) CHECK(rc_runtime_call_integer_batch(r,other,items,2,out,2,&done,NULL)==RC_OK);
        CHECK(rc_runtime_remove(r,other,NULL)==RC_OK);
        enum rc_status s=rc_runtime_invalidate(r,h,NULL); CHECK(s==RC_OK || s==RC_BUSY);
        s=rc_runtime_clear(r,NULL); CHECK(s==RC_OK || s==RC_BUSY);
    }
    for(unsigned n=0;n<4;++n) CHECK(pthread_join(threads[n],NULL)==0);
    struct rc_function_stats a,b; CHECK(rc_runtime_function_stats(r,h,&a,NULL)==RC_OK);
    CHECK(rc_runtime_function_stats(r,alias,&b,NULL)==RC_OK && a.invocations+b.invocations==4*200*128);
    CHECK(rc_runtime_clear(r,NULL)==RC_OK); struct rc_runtime_stats stats;
    CHECK(rc_runtime_get_stats(r,&stats,NULL)==RC_OK && stats.active_calls==0 && stats.executable_bytes==0);
    CHECK(rc_runtime_destroy(&r,NULL)==RC_OK);
}
int main(void)
{
    alarm(60); test_inputs(); test_results(); test_bounds(); test_guard_page(); test_sampling(); test_concurrency(false); test_concurrency(true);
    puts("batch preflight, aliases, partial faults, independent results, policies and concurrent cache pressure passed"); return 0;
}
