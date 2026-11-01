#include "integer.h"

#include <stdlib.h>
#include <string.h>

static void check(bool ok) { if (!ok) abort(); }
static uint64_t u64(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n) value |= (uint64_t)data[n] << (n*8);
    return value;
}
static bool forward(const struct rc_program *p)
{
    for (size_t n = 0; n < p->count; ++n)
        if (rc_is_branch(p->insns[n].op) && p->insns[n].immediate <= n) return false;
    return true;
}
static void accumulator(struct rc_program *p, uint64_t argument)
{
    uint64_t result = 91;
    enum rc_status status = rc_interpret(p,argument,256,&result,NULL);
    if (status != RC_OK) { check(result == 91); return; }
    size_t length;
    check(rc_emit(p,NULL,0,&length,NULL) == RC_OK);
    uint8_t *bytes = malloc(length+1); check(bytes != NULL); memset(bytes,0xa5,length+1);
    check(rc_emit(p,bytes,length-1,&length,NULL) == RC_CAPACITY && bytes[0] == 0xa5);
    check(rc_emit(p,bytes,length,&length,NULL) == RC_OK && bytes[length] == 0xa5);
    if (forward(p)) {
        struct rc_code code = {0};
        /* Compile emitted supported instructions after the ENDBR marker. */
        enum rc_status compiled = rc_compile(bytes+4,length-4,false,&code,NULL);
        if (compiled == RC_OK) check(code.function(argument) == result);
        check(rc_code_free(&code,NULL) == RC_OK);
    }
    free(bytes);
    check(rc_optimize(p,NULL) == RC_OK);
    uint64_t optimized = 91;
    if (rc_interpret(p,argument,256,&optimized,NULL) == RC_OK) check(optimized == result);
}
static void integer(const uint8_t *data, size_t size)
{
    if (size < 19) return;
    struct rc_builder *builder = NULL; struct rc_integer_program *p = NULL;
    size_t registers = 1+data[0]%64, arguments = data[1]%3;
    check(rc_builder_create(registers,arguments,&builder,NULL) == RC_OK);
    struct rc_integer_insn insns[32]; size_t count = (size-19)/14;
    if (count > 32) count = 32;
    bool memory = false, appending = true;
    for (size_t n = 0; n < count; ++n) {
        const uint8_t *b = data+19+n*14;
        insns[n] = (struct rc_integer_insn){.op=(enum rc_integer_op)b[0],.dst=b[1],.left=b[2],
            .right=b[3],.target=(uint32_t)b[4] | (uint32_t)b[5]<<8,.immediate=u64(b+6)};
        memory |= rc_integer_load(insns[n].op);
        if (appending && rc_builder_append(builder,insns[n],NULL) != RC_OK) appending = false;
    }
    /* The private emitter must reject malformed descriptors before table access. */
    struct rc_integer_program raw = {registers,arguments,count,memory,insns};
    size_t length = 0; enum rc_status valid = rc_integer_validate(&raw,NULL);
    enum rc_status emitted = rc_integer_emit(&raw,NULL,0,&length,NULL);
    check(emitted == valid);
    if (rc_builder_finalize(builder,&p,NULL) == RC_OK) {
        uint64_t args[] = {u64(data+2),u64(data+10)}, result = 91;
        struct rc_memory_view view = {data,size > 64 ? 64 : size};
        enum rc_status status = rc_integer_interpret_view(p,args,arguments,&view,256,&result,NULL);
        bool acyclic = true;
        for (size_t n = 0; n < p->count; ++n)
            if ((p->insns[n].op == RC_INT_JUMP || p->insns[n].op == RC_INT_BRANCH) && p->insns[n].target <= n)
                acyclic = false;
        if (acyclic) {
            check(status == RC_OK || status == RC_BOUNDS);
            struct rc_code code = {0}; rc_integer_function native; bool allocated;
            check(rc_integer_emit(p,NULL,0,&length,NULL) == RC_OK);
            if (rc_integer_map(p,length,&code,&native,&allocated,NULL) == RC_OK) {
                if (p->memory) {
                    rc_integer_view_function fn; memcpy(&fn,&native,sizeof(fn));
                    struct rc_integer_result output = {91,0};
                    struct rc_error error;
                    check(rc_integer_interpret_view(p,args,arguments,&view,256,&result,&error) == status);
                    check(fn(args[0],args[1],view.data,view.length,&output) == status);
                    check(status == RC_OK ? output.value == result : output.value == 91 && output.offset == error.offset);
                } else check(status == RC_OK && native(args[0],args[1]) == result);
            }
            check(rc_code_free(&code,NULL) == RC_OK);
        } else if (status != RC_OK) check(result == 91);
        rc_integer_program_destroy(p);
    }
    rc_builder_destroy(builder);
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0 || size > 512) return 0;
    if (data[0]%3 == 2) { integer(data+1,size-1); return 0; }
    struct rc_program p = {0};
    if (data[0]%3 == 0) {
        if (rc_decode(data+1,size-1,&p,NULL) == RC_OK) accumulator(&p,UINT64_MAX);
        rc_program_free(&p);
    } else {
        struct rc_insn insns[32]; size_t count = (size-1)/5;
        if (count > 32) count = 32;
        for (size_t n = 0; n < count; ++n) {
            const uint8_t *b = data+1+n*5;
            insns[n] = (struct rc_insn){(enum rc_op)b[0],(uint32_t)b[1] | (uint32_t)b[2]<<8 |
                (uint32_t)b[3]<<16 | (uint32_t)b[4]<<24};
        }
        p = (struct rc_program){insns,count};
        if (rc_validate(&p,NULL) == RC_OK) accumulator(&p,UINT64_MAX);
    }
    return 0;
}
