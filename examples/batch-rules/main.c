#define _POSIX_C_SOURCE 200809L
#include "recode_integer.h"
#include "rule.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check(enum rc_status status)
{
    if(status!=RC_OK) { fprintf(stderr,"batch rules: %s\n",rc_status_string(status)); exit(1); }
}
static uint64_t now(void)
{
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)!=0) exit(1);
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static struct rc_integer_program *build(void)
{
    struct rc_builder *b=NULL; struct rc_integer_program *p=NULL;
    check(rc_builder_create(3,1,&b,NULL));
    for(size_t n=0;n<sizeof(rule_insns)/sizeof(*rule_insns);++n) check(rc_builder_append(b,rule_insns[n],NULL));
    check(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b); return p;
}
static uint64_t reference(const uint8_t *record, uint64_t threshold)
{
    uint64_t score=0;
    for(unsigned n=8;n-- >0;) score=score*256+record[n];
    return record[8]!=0 && score>threshold ? score-threshold : 0;
}
static void evaluate(struct rc_runtime *r, rc_handle h, const struct rc_integer_batch_item *items,
                       size_t count, uint64_t *results, bool batch)
{
    if(batch) {
        size_t completed; struct rc_error error;
        enum rc_status status=rc_runtime_call_integer_batch(r,h,items,count,results,count,&completed,&error);
        if(status!=RC_OK) {
            fprintf(stderr,"item %zu, instruction %zu: %s\n",completed,error.offset,rc_status_string(status)); exit(1);
        }
        if(completed!=count) exit(1);
    } else for(size_t n=0;n<count;++n)
        check(rc_runtime_call_integer_view(r,h,items[n].arguments,1,items[n].view,&results[n],NULL));
}
static void run(const struct rc_integer_program *p, size_t count, enum rc_policy policy,
                  bool batch, unsigned round, bool benchmark)
{
    uint8_t (*records)[9]=malloc(count*sizeof(*records));
    uint64_t *thresholds=malloc(count*sizeof(*thresholds)), *results=malloc(count*sizeof(*results));
    struct rc_memory_view *views=malloc(count*sizeof(*views));
    struct rc_integer_batch_item *items=malloc(count*sizeof(*items));
    if(records==NULL || thresholds==NULL || results==NULL || views==NULL || items==NULL) exit(1);
    for(size_t n=0;n<count;++n) {
        uint64_t score=(uint64_t)n*UINT64_C(0x9e3779b97f4a7c15);
        for(unsigned byte=0;byte<8;++byte) records[n][byte]=(uint8_t)(score>>(byte*8));
        records[n][8]=(uint8_t)(n%3); thresholds[n]=score^(score>>7);
        views[n]=(struct rc_memory_view){records[n],sizeof(records[n])};
        items[n]=(struct rc_integer_batch_item){&thresholds[n],1,&views[n]};
    }
    struct rc_runtime_config config=rc_runtime_default_config(); config.policy=policy;
    struct rc_runtime *r=NULL; rc_handle h; uint64_t total=now();
    check(rc_runtime_create(&config,&r,NULL)); check(rc_runtime_register_integer(r,p,&h,NULL));
    uint64_t start=now(); evaluate(r,h,items,count,results,batch);
    uint64_t cold=now()-start, end_to_end=now()-total;
    for(size_t n=0;n<count;++n) if(results[n]!=reference(records[n],thresholds[n])) exit(1);
    for(size_t warm=0;warm<8 || warm*count<128;++warm) evaluate(r,h,items,count,results,batch);
    size_t repeats=32768/count; if(repeats<8) repeats=8;
    start=now();
    for(size_t repeat=0;repeat<repeats;++repeat) evaluate(r,h,items,count,results,batch);
    uint64_t duration=now()-start;
    for(size_t n=0;n<count;++n) if(results[n]!=reference(records[n],thresholds[n])) exit(1);
    struct rc_function_stats f; struct rc_runtime_stats s;
    check(rc_runtime_function_stats(r,h,&f,NULL)); check(rc_runtime_get_stats(r,&s,NULL));
    if(benchmark)
        printf("%u,%d,%s,%zu,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.3f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu\n",
               round,(int)policy,batch?"batch":"scalar",count,repeats,cold,end_to_end,duration,
               (double)duration/(double)(count*repeats),f.compilation_ns,f.interpreted,f.jitted,s.peak_executable_bytes);
    else printf("checked %zu records, interpreted=%" PRIu64 ", jitted=%" PRIu64 ", peak executable=%zu bytes\n",count,f.interpreted,f.jitted,s.peak_executable_bytes);
    /* Failed items and later output remain unchanged; the offset is an IR index. */
    views[0].length=0; results[0]=91; size_t completed; struct rc_error error;
    if(rc_runtime_call_integer_batch(r,h,items,count,results,count,&completed,&error)!=RC_BOUNDS ||
       completed!=0 || results[0]!=91 || error.offset!=2) exit(1);
    check(rc_runtime_remove(r,h,NULL)); check(rc_runtime_destroy(&r,NULL));
    free(items); free(views); free(results); free(thresholds); free(records);
}
int main(int argc, char **argv)
{
    bool benchmark=argc==2 && strcmp(argv[1],"--bench")==0;
    if(argc!=1 && !benchmark) return 1;
    struct rc_integer_program *p=build();
    if(!benchmark) run(p,256,RC_POLICY_FIXED,true,0,false);
    else {
        const size_t counts[]={1,8,32,128,1024,4096,65536};
        puts("round,policy,path,items,repeats,cold_ns,cold_total_ns,warm_ns,warm_ns_per_item,compile_ns,interpreted,jitted,peak_executable_bytes");
        for(unsigned round=0;round<3;++round) for(size_t c=0;c<7;++c)
            for(enum rc_policy policy=RC_POLICY_INTERPRET;policy<=RC_POLICY_ADAPTIVE;++policy)
                for(unsigned order=0;order<2;++order) run(p,counts[c],policy,(round+order)%2!=0,round+1,true);
    }
    rc_integer_program_destroy(p); return 0;
}
