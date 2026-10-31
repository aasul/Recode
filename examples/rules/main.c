#include <recode_integer.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void require(enum rc_status status)
{
    if (status != RC_OK) {
        fprintf(stderr,"%s\n",rc_status_string(status));
        exit(1);
    }
}

static uint64_t reference(unsigned rule, uint64_t a, uint64_t b)
{
    switch (rule) {
    case 0: return a > b ? a + 2*b : a ^ b;
    case 1: return ((a & b) + (a | b)) ^ (a - b);
    default: return a == b ? (a << (b & 63)) : (b >> (a & 63));
    }
}

int main(void)
{
    const struct rc_integer_insn rules[][10] = {
        {{.op=RC_INT_ARGUMENT}, {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
         {.op=RC_INT_ULT,.dst=2,.left=1,.right=0}, {.op=RC_INT_BRANCH,.left=2,.target=6},
         {.op=RC_INT_XOR,.dst=3,.right=1}, {.op=RC_INT_RETURN,.left=3},
         {.op=RC_INT_CONSTANT,.dst=2,.immediate=2}, {.op=RC_INT_MUL,.dst=2,.left=2,.right=1},
         {.op=RC_INT_ADD,.dst=3,.right=2}, {.op=RC_INT_RETURN,.left=3}},
        {{.op=RC_INT_ARGUMENT}, {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
         {.op=RC_INT_AND,.dst=2,.right=1}, {.op=RC_INT_OR,.dst=3,.right=1},
         {.op=RC_INT_ADD,.dst=2,.left=2,.right=3}, {.op=RC_INT_SUB,.dst=3,.right=1},
         {.op=RC_INT_XOR,.dst=2,.left=2,.right=3}, {.op=RC_INT_RETURN,.left=2}},
        {{.op=RC_INT_ARGUMENT}, {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
         {.op=RC_INT_EQ,.dst=2,.right=1}, {.op=RC_INT_BRANCH,.left=2,.target=6},
         {.op=RC_INT_SHR,.dst=3,.left=1}, {.op=RC_INT_RETURN,.left=3},
         {.op=RC_INT_SHL,.dst=3,.right=1}, {.op=RC_INT_RETURN,.left=3}}
    };
    const size_t counts[] = {10,8,8};
    const uint64_t values[] = {0,1,2,63,64,UINT64_MAX,UINT64_C(0x8000000000000000)};
    for (enum rc_policy policy = RC_POLICY_INTERPRET; policy <= RC_POLICY_ADAPTIVE; ++policy) {
        struct rc_runtime_config config = rc_runtime_default_config();
        config.policy = policy; config.hot_threshold = 2;
        struct rc_runtime *runtime = NULL;
        require(rc_runtime_create(&config,&runtime,NULL));
        for (unsigned rule = 0; rule < 3; ++rule) {
            struct rc_builder *builder = NULL;
            struct rc_integer_program *program = NULL;
            require(rc_builder_create(4,2,&builder,NULL));
            for (size_t n = 0; n < counts[rule]; ++n)
                require(rc_builder_append(builder,rules[rule][n],NULL));
            require(rc_builder_finalize(builder,&program,NULL));
            rc_builder_destroy(builder);
            rc_handle handle;
            require(rc_runtime_register_integer(runtime,program,&handle,NULL));
            rc_integer_program_destroy(program);
            uint64_t checksum = 0;
            for (unsigned repeat = 0; repeat < 100; ++repeat)
                for (size_t a = 0; a < 7; ++a)
                    for (size_t b = 0; b < 7; ++b) {
                        uint64_t args[] = {values[a],values[b]}, result;
                        require(rc_runtime_call_integer(runtime,handle,args,2,&result,NULL));
                        if (result != reference(rule,args[0],args[1])) return 1;
                        checksum += result;
                    }
            struct rc_function_stats stats;
            require(rc_runtime_function_stats(runtime,handle,&stats,NULL));
            if (policy == RC_POLICY_FIXED && stats.jitted == 0) return 1;
            printf("policy %d rule %u: checksum %" PRIu64 ", native calls %" PRIu64 "\n",
                   (int)policy,rule,checksum,stats.jitted);
            require(rc_runtime_remove(runtime,handle,NULL));
        }
        require(rc_runtime_destroy(&runtime,NULL));
    }
    return 0;
}
