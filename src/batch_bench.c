#define _POSIX_C_SOURCE 200809L
#include "integer.h"
#include "rule.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check(enum rc_status status) { if(status!=RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); } }
static uint64_t now(void)
{
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)!=0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static enum rc_status reference(uint64_t threshold, uint64_t unused, const void *data,
                                 size_t length, struct rc_integer_result *out)
{
    (void)unused;
    if(length<9) { out->offset=2; return RC_BOUNDS; }
    const uint8_t *bytes=data; uint64_t score=0;
    for(unsigned n=8;n-- >0;) score=score*256+bytes[n];
    out->value=bytes[8]!=0 && score>threshold ? score-threshold : 0; return RC_OK;
}
static void kernels(void)
{
    struct rc_builder *b=NULL; struct rc_integer_program *p=NULL;
    check(rc_builder_create(3,1,&b,NULL));
    for(size_t n=0;n<sizeof(rule_insns)/sizeof(*rule_insns);++n) check(rc_builder_append(b,rule_insns[n],NULL));
    check(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
    struct rc_code code={0}; rc_integer_function raw; bool allocated; size_t size;
    check(rc_integer_emit(p,NULL,0,&size,NULL)); check(rc_integer_map(p,size,&code,&raw,&allocated,NULL));
    rc_integer_view_function kernel; memcpy(&kernel,&raw,sizeof(kernel));
    const size_t counts[]={1,8,32,128,1024,4096,65536};
    puts("round,path,items,repeats,total_ns,ns_per_item,code_bytes");
    for(unsigned round=0;round<3;++round) for(size_t c=0;c<7;++c) {
        size_t count=counts[c], repeats=32768/count; if(repeats<8) repeats=8;
        uint8_t (*records)[9]=malloc(count*sizeof(*records)); uint64_t *thresholds=malloc(count*sizeof(*thresholds));
        uint64_t *results=malloc(count*sizeof(*results)), *expected=malloc(count*sizeof(*expected));
        if(records==NULL || thresholds==NULL || results==NULL || expected==NULL) exit(1);
        for(size_t n=0;n<count;++n) {
            uint64_t score=(uint64_t)n*UINT64_C(0x9e3779b97f4a7c15);
            for(unsigned byte=0;byte<8;++byte) records[n][byte]=(uint8_t)(score>>(byte*8));
            records[n][8]=(uint8_t)(n%3); thresholds[n]=score^(score>>7);
            expected[n]=n%3!=0 && score>thresholds[n] ? score-thresholds[n] : 0;
        }
        for(unsigned order=0;order<2;++order) {
            bool native=(round+order)%2!=0;
            rc_integer_view_function volatile entry=native ? kernel : reference;
            uint64_t start=now();
            for(size_t repeat=0;repeat<repeats;++repeat) for(size_t n=0;n<count;++n) {
                struct rc_integer_result out; check(entry(thresholds[n],0,records[n],9,&out)); results[n]=out.value;
            }
            uint64_t elapsed=now()-start;
            for(size_t n=0;n<count;++n) if(results[n]!=expected[n]) exit(1);
            printf("%u,%s,%zu,%zu,%" PRIu64 ",%.3f,%zu\n",round+1,native?"kernel":"c",count,repeats,elapsed,
                   (double)elapsed/(double)(count*repeats),native?size:0);
        }
        free(expected); free(results); free(thresholds); free(records);
    }
    check(rc_code_free(&code,NULL)); rc_integer_program_destroy(p);
}
struct worker { struct rc_runtime *r; rc_handle h; bool batch; unsigned id; };
static void *execute(void *context)
{
    struct worker *w=context; uint64_t args[128][2], results[128]; struct rc_integer_batch_item items[128];
    for(size_t n=0;n<128;++n) { args[n][0]=n; args[n][1]=UINT64_MAX-w->id; items[n]=(struct rc_integer_batch_item){args[n],2,NULL}; }
    for(unsigned repeat=0;repeat<256;++repeat) {
        if(w->batch) { size_t done; check(rc_runtime_call_integer_batch(w->r,w->h,items,128,results,128,&done,NULL)); if(done!=128) exit(1); }
        else for(size_t n=0;n<128;++n) check(rc_runtime_call_integer(w->r,w->h,args[n],2,&results[n],NULL));
        for(size_t n=0;n<128;++n) if(results[n]!=args[n][0]+args[n][1]) exit(1);
    }
    return NULL;
}
static void threads(void)
{
    struct rc_builder *b=NULL; struct rc_integer_program *p=NULL;
    const struct rc_integer_insn insns[]={{.op=RC_INT_ARGUMENT},{.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    check(rc_builder_create(2,2,&b,NULL)); for(size_t n=0;n<4;++n) check(rc_builder_append(b,insns[n],NULL));
    check(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
    puts("round,policy,path,threads,items_per_thread,total_ns,ns_per_item,interpreted,jitted");
    for(unsigned round=0;round<3;++round) for(unsigned count=1;count<=4;count*=2)
        for(enum rc_policy policy=RC_POLICY_INTERPRET;policy<=RC_POLICY_ADAPTIVE;++policy)
            for(unsigned order=0;order<2;++order) {
                bool batch=(round+order)%2!=0; struct rc_runtime_config c=rc_runtime_default_config(); c.policy=policy;
                struct rc_runtime *r=NULL; rc_handle h; check(rc_runtime_create(&c,&r,NULL)); check(rc_runtime_register_integer(r,p,&h,NULL));
                struct worker warm={r,h,batch,0}; execute(&warm);
                pthread_t ids[4]; struct worker workers[4]; uint64_t start=now();
                for(unsigned n=0;n<count;++n) { workers[n]=(struct worker){r,h,batch,n}; if(pthread_create(&ids[n],NULL,execute,&workers[n])!=0) exit(1); }
                for(unsigned n=0;n<count;++n) if(pthread_join(ids[n],NULL)!=0) exit(1);
                uint64_t elapsed=now()-start; struct rc_function_stats stats; check(rc_runtime_function_stats(r,h,&stats,NULL));
                printf("%u,%d,%s,%u,32768,%" PRIu64 ",%.3f,%" PRIu64 ",%" PRIu64 "\n",round+1,(int)policy,batch?"batch":"scalar",count,elapsed,
                       (double)elapsed/(double)(count*32768),stats.interpreted,stats.jitted);
                check(rc_runtime_destroy(&r,NULL));
            }
    rc_integer_program_destroy(p);
}
int main(int argc, char **argv)
{
    if(argc==1) kernels(); else if(argc==2 && strcmp(argv[1],"--threads")==0) threads(); else return 1;
    return 0;
}
