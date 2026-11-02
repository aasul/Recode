#include "bench.h"
#include <mir.h>
#include <mir-gen.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct text { char bytes[16384]; size_t length; };
static void append(struct text *t, const char *format, ...)
{
    va_list args; va_start(args,format);
    int n=vsnprintf(t->bytes+t->length,sizeof(t->bytes)-t->length,format,args); va_end(args);
    if (n<0 || (size_t)n>=sizeof(t->bytes)-t->length) exit(1);
    t->length+=(size_t)n;
}
void *mir_open(void)
{
    MIR_context_t c=MIR_init(); MIR_gen_init(c); MIR_gen_set_optimize_level(c,2); return c;
}
entry_fn mir_build(void *state, const struct workload *w, struct metrics *m)
{
    MIR_context_t c=state; uint64_t start=bench_now(); struct text t={{0},0};
    append(&t,"comparison: module\n export evaluate\nevaluate: func u64, u64:a, u64:b, p:data, u64:length\n local i64:r0, i64:r1, i64:r2, i64:r3, i64:r4, i64:r5, i64:r6, i64:r7, i64:remaining\n");
    for (size_t n=0; n<w->count; ++n) {
        struct rc_integer_insn i=w->insns[n]; append(&t,"L%zu:\n",n);
        switch (i.op) {
        case RC_INT_ARGUMENT: append(&t," mov r%u, %s\n",i.dst,i.immediate == 0 ? "a" : "b"); break;
        case RC_INT_CONSTANT: append(&t," mov r%u, 0x%" PRIx64 "\n",i.dst,i.immediate); break;
        case RC_INT_BRANCH: append(&t," bne L%u, r%u, 0\n",i.target,i.left); break;
        case RC_INT_JUMP: append(&t," jmp L%u\n",i.target); break;
        case RC_INT_RETURN: append(&t," ret r%u\n",i.left); break;
        case RC_INT_LOAD_U8:
        case RC_INT_LOAD_U64: {
            unsigned width=i.op == RC_INT_LOAD_U8 ? 1 : 8;
            append(&t," ubgt failure, r%u, length\n sub remaining, length, r%u\n ublt failure, remaining, %u\n mov r%u, u%u:(data,r%u)\n",i.left,i.left,width,i.dst,width*8,i.left); break;
        }
        default: {
            const char *op=NULL;
            switch (i.op) {
            case RC_INT_ADD: op="add"; break; case RC_INT_SUB: op="sub"; break;
            case RC_INT_MUL: op="mul"; break; case RC_INT_AND: op="and"; break;
            case RC_INT_OR: op="or"; break; case RC_INT_XOR: op="xor"; break;
            case RC_INT_ULT: op="ult"; break; case RC_INT_ULE: op="ule"; break;
            default: fprintf(stderr,"MIR adapter: unsupported workload op\n"); exit(1);
            }
            append(&t," %s r%u, r%u, r%u\n",op,i.dst,i.left,i.right); break;
        }
        }
    }
    append(&t,"failure:\n ret 0xffffffffffffffff\n endfunc\n endmodule\n");
    MIR_scan_string(c,t.bytes); /* MIR parsing/finalization performs validation. */
    m->build_ns=bench_now()-start;
    MIR_module_t module=DLIST_TAIL(MIR_module_t,*MIR_get_module_list(c));
    MIR_item_t function=DLIST_TAIL(MIR_item_t,module->items);
    start=bench_now(); MIR_load_module(c,module); MIR_link(c,MIR_set_gen_interface,NULL);
    void *address=MIR_gen(c,function); m->compile_ns=bench_now()-start;
    entry_fn result; _Static_assert(sizeof(result) == sizeof(address),"native pointer");
    memcpy(&result,&address,sizeof(result)); return result;
}
void mir_close(void *state)
{
    MIR_gen_finish(state); MIR_finish(state);
}
