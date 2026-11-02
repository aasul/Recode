#define _GNU_SOURCE
#include "bench.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
static struct rc_integer_program *build(const struct rc_integer_insn *insns, size_t count, size_t arity)
{
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    CHECK(rc_builder_create(8,arity,&b,NULL) == RC_OK);
    for (size_t n = 0; n < count; ++n) CHECK(rc_builder_append(b,insns[n],NULL) == RC_OK);
    CHECK(rc_builder_finalize(b,&p,NULL) == RC_OK); rc_builder_destroy(b); return p;
}
static void workloads(void)
{
    const size_t strides[] = {33,64,129};
    for (unsigned kind = 0; kind < 6; ++kind) for (size_t s = 0; s < 3; ++s)
        for (enum rc_policy policy = RC_POLICY_INTERPRET; policy <= RC_POLICY_ADAPTIVE; ++policy) {
            enum { COUNT = 129 };
            size_t stride = strides[s]; uint8_t *records = calloc(COUNT,stride);
            uint64_t out[COUNT], descriptor_out[COUNT]; struct input in[COUNT];
            struct rc_memory_view views[COUNT]; struct rc_integer_batch_item items[COUNT];
            CHECK(records != NULL);
            for (size_t n = 0; n < COUNT; ++n) {
                inputs(n,&in[n]); memcpy(records+n*stride+1,in[n].args,16);
                memcpy(records+n*stride+17,in[n].data,16);
                views[n] = (struct rc_memory_view){in[n].data,16};
                items[n] = (struct rc_integer_batch_item){in[n].args,2,&views[n]};
            }
            struct workload w = workload(kind); struct rc_integer_program *p = build(w.insns,w.count,2);
            struct rc_runtime_config c = rc_runtime_default_config(); c.policy = policy;
            c.hot_threshold = 2; c.cold_batch_compile = true; c.initial_compile_ns = 1;
            struct rc_runtime *r = NULL; rc_handle h; size_t done;
            CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK && rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
            rc_integer_program_destroy(p);
            struct rc_integer_record_batch layout = {records,COUNT,stride,33,2,{1,9},17,16};
            for (unsigned repeat = 0; repeat < 4; ++repeat) {
                CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,&done,NULL) == RC_OK && done == COUNT);
                CHECK(rc_runtime_call_integer_batch(r,h,items,COUNT,descriptor_out,COUNT,&done,NULL) == RC_OK);
                for (size_t n = 0; n < COUNT; ++n) CHECK(out[n] == reference(kind,&in[n]) && out[n] == descriptor_out[n]);
            }
            CHECK(rc_runtime_destroy(&r,NULL) == RC_OK); free(records);
        }
}
static void bounds_and_layout(void)
{
    const struct rc_integer_insn insns[] = {{.op=RC_INT_ARGUMENT},{.op=RC_INT_LOAD_U64},{.op=RC_INT_RETURN}};
    struct rc_integer_program *p = build(insns,3,1);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *mapping = mmap(NULL,2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(mapping != MAP_FAILED && mprotect(mapping,page,PROT_READ|PROT_WRITE) == 0);
    enum { COUNT = 33 }; uint8_t *records = mapping+page-COUNT*17;
    for (size_t n = 0; n < COUNT; ++n) { memset(records+n*17,0,9); memset(records+n*17+9,7,8); }
    struct rc_integer_record_batch layout = {records,COUNT,17,17,1,{1,0},9,8};
    for (unsigned budget = 0; budget < 2; ++budget) {
        struct rc_runtime_config c = rc_runtime_default_config(); c.policy = RC_POLICY_FIXED;
        c.hot_threshold = 2; c.executable_budget = budget*page;
        struct rc_runtime *r = NULL; rc_handle h; size_t done; uint64_t out[COUNT]; struct rc_error error;
        CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK && rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,&done,NULL) == RC_OK);
            for (size_t fail = 0; fail < COUNT; ++fail) {
                uint64_t bad = fail%2 ? UINT64_MAX : 1; memcpy(records+fail*17+1,&bad,8);
                for (size_t n = 0; n < COUNT; ++n) out[n] = 91;
                struct rc_function_stats before,after;
                CHECK(rc_runtime_function_stats(r,h,&before,NULL) == RC_OK);
                CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,&done,&error) == RC_BOUNDS);
                CHECK(rc_runtime_function_stats(r,h,&after,NULL) == RC_OK && after.invocations-before.invocations == fail+1);
                CHECK(after.interpreted+after.jitted == after.invocations);
                CHECK(done == fail && error.offset == 1 && error.system_errno == 0);
                for (size_t n = 0; n < COUNT; ++n) CHECK(out[n] == (n < fail ? UINT64_C(0x0707070707070707) : 91));
                bad = 0; memcpy(records+fail*17+1,&bad,8);
            }
        }
        const struct rc_integer_record_batch good = layout;
        for (size_t n = 0; n < COUNT; ++n) out[n] = 91;
        for (unsigned invalid = 0; invalid < 11; ++invalid) {
            layout = good;
            switch (invalid) {
            case 0: layout.stride = 16; break;
            case 1: layout.argument_offsets[0] = 10; break;
            case 2: layout.argument_offsets[0] = SIZE_MAX; break;
            case 3: layout.view_offset = 18; break;
            case 4: layout.view_length = 9; break;
            case 5: layout.argument_count = 3; break;
            case 6: layout.records = NULL; break;
            case 7: layout.records = (const void *)(uintptr_t)(UINTPTR_MAX-3); break;
            case 8: layout.record_count = SIZE_MAX; break;
            case 9: layout.stride = SIZE_MAX; break;
            case 10: layout.argument_count = 0; break;
            }
            done = 99;
            CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,&done,&error) == RC_ARGUMENT && done == 0);
            for (size_t n = 0; n < COUNT; ++n) CHECK(out[n] == 91);
        }
        layout = good; done = 99;
        CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT-1,&done,NULL) == RC_CAPACITY && done == 0);
        done = 99;
        CHECK(rc_runtime_call_integer_records(r,h,&layout,(uint64_t *)(records+1),COUNT,&done,NULL) == RC_ARGUMENT && done == 99);
        CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,(size_t *)(records+1),NULL) == RC_ARGUMENT);
        CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,&done,(struct rc_error *)(records+1)) == RC_ARGUMENT && done == 99);
        layout.record_count = 1;
        CHECK(rc_runtime_call_integer_records(r,h,&layout,(uint64_t *)&layout,1,&done,NULL) == RC_ARGUMENT && done == 99);
        CHECK(rc_runtime_call_integer_records(r,h,NULL,out,COUNT,&done,NULL) == RC_ARGUMENT && done == 0);
        CHECK(rc_runtime_call_integer_records(NULL,h,&layout,out,COUNT,&done,NULL) == RC_ARGUMENT && done == 0);
        CHECK(rc_runtime_call_integer_records(r,h,&layout,out,COUNT,NULL,NULL) == RC_ARGUMENT);
        layout.record_count = 0; layout.records = NULL;
        CHECK(rc_runtime_call_integer_records(r,h,&layout,NULL,0,&done,NULL) == RC_OK && done == 0);
        CHECK(rc_runtime_call_integer_records(r,0,&layout,NULL,0,&done,NULL) == RC_NOT_FOUND);
        CHECK(rc_runtime_destroy(&r,NULL) == RC_OK); layout = good;
    }
    CHECK(munmap(mapping,2*page) == 0); rc_integer_program_destroy(p);
    const struct rc_integer_insn constant[] = {{.op=RC_INT_CONSTANT,.immediate=77},{.op=RC_INT_RETURN}};
    p = build(constant,2,0); struct rc_runtime_config c = rc_runtime_default_config(); c.policy = RC_POLICY_FIXED; c.hot_threshold = 2;
    struct rc_runtime *r = NULL; rc_handle h; size_t done; uint64_t out[33];
    CHECK(rc_runtime_create(&c,&r,NULL) == RC_OK && rc_runtime_register_integer(r,p,&h,NULL) == RC_OK);
    layout = (struct rc_integer_record_batch){.record_count=33};
    for (unsigned n = 0; n < 3; ++n) {
        CHECK(rc_runtime_call_integer_records(r,h,&layout,out,33,&done,NULL) == RC_OK && done == 33);
        for (size_t i = 0; i < 33; ++i) CHECK(out[i] == 77);
    }
    CHECK(rc_runtime_destroy(&r,NULL) == RC_OK); rc_integer_program_destroy(p);
}
int main(void)
{
    workloads(); bounds_and_layout();
    puts("strided differential workloads, unaligned arguments, guard pages, prefix faults and structural ranges passed");
    return 0;
}
