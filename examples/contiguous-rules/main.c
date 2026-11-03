#include <recode_integer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct record { uint64_t threshold; uint8_t data[9]; };
static void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"%s\n",rc_status_string(status)); exit(1); }
}
int main(void)
{
    enum { COUNT = 256 };
    struct record *records = calloc(COUNT,sizeof(*records)); uint64_t *results = malloc(COUNT*sizeof(*results));
    if (records == NULL || results == NULL) { free(records); free(results); return 1; }
    uint64_t expected[COUNT];
    for (size_t n = 0; n < COUNT; ++n) {
        uint64_t score = (uint64_t)n*UINT64_C(0x9e3779b97f4a7c15);
        records[n].threshold = score/2; records[n].data[8] = (uint8_t)(n%3);
        for (unsigned byte = 0; byte < 8; ++byte) records[n].data[byte] = (uint8_t)(score>>(8*byte));
        expected[n] = n%3 != 0 && score > records[n].threshold ? score-records[n].threshold : 0;
    }
    const struct rc_integer_insn rule[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_CONSTANT,.dst=1,.immediate=8},{.op=RC_INT_LOAD_U8,.dst=2,.left=1},
        {.op=RC_INT_BRANCH,.left=2,.target=6},{.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},
        {.op=RC_INT_CONSTANT,.dst=1},{.op=RC_INT_LOAD_U64,.dst=1,.left=1},
        {.op=RC_INT_ULT,.dst=2,.right=1},{.op=RC_INT_BRANCH,.left=2,.target=12},
        {.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},{.op=RC_INT_SUB,.left=1},{.op=RC_INT_RETURN}};
    struct rc_builder *b = NULL; struct rc_integer_program *p = NULL;
    require(rc_builder_create(3,1,&b,NULL));
    for (size_t n = 0; n < sizeof(rule)/sizeof(*rule); ++n) require(rc_builder_append(b,rule[n],NULL));
    require(rc_builder_finalize(b,&p,NULL)); rc_builder_destroy(b);
    struct rc_integer_record_batch layout = {.records=records,.record_count=COUNT,.stride=sizeof(*records),
        .record_size=sizeof(*records),.argument_count=1,.argument_offsets={offsetof(struct record,threshold),0},
        .view_offset=offsetof(struct record,data),.view_length=sizeof(records[0].data)};
    for (enum rc_policy policy = RC_POLICY_INTERPRET; policy <= RC_POLICY_ADAPTIVE; ++policy) {
        struct rc_runtime_config c = rc_runtime_default_config(); c.policy = policy;
        struct rc_runtime *r = NULL; rc_handle h; size_t done;
        require(rc_runtime_create(&c,&r,NULL)); require(rc_runtime_register_integer(r,p,&h,NULL));
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            require(rc_runtime_call_integer_records(r,h,&layout,results,COUNT,&done,NULL));
            if (done != COUNT || memcmp(results,expected,sizeof(expected)) != 0) return 1;
        }
        layout.view_length = 8; for (size_t n = 0; n < COUNT; ++n) results[n] = 91;
        struct rc_error error;
        if (rc_runtime_call_integer_records(r,h,&layout,results,COUNT,&done,&error) != RC_BOUNDS || done != 0 || error.offset != 2) return 1;
        for (size_t n = 0; n < COUNT; ++n) if (results[n] != 91) return 1;
        layout.view_length = 9; require(rc_runtime_destroy(&r,NULL));
    }
    rc_integer_program_destroy(p); free(results); free(records);
    puts("contiguous rules match C in every policy; failed records preserve outputs");
    return 0;
}
