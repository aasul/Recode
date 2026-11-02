#include "bench.h"
#include <string.h>

void inputs(size_t n, struct input *in)
{
    uint64_t x = (uint64_t)n * UINT64_C(0x9e3779b97f4a7c15);
    in->args[0] = x;
    in->args[1] = n % 3 == 0 ? x : (x >> 17) ^ UINT64_C(0xdeadbeef);
    for (size_t i = 0; i < sizeof(in->data); ++i) in->data[i] = (uint8_t)(x >> ((i % 8) * 8));
}
uint64_t reference(unsigned kind, const struct input *in)
{
    uint64_t a = in->args[0], b = in->args[1];
    switch (kind) {
    case 0: return a + 2*b;
    case 1: return ((a & b) + (a | b)) ^ (a-b);
    case 2: return a > b ? a + 2*b : a ^ b;
    case 3:
        for (unsigned n = 0; n < 16; ++n) if (a < b) a += b;
        return a;
    case 4:
        for (unsigned n = 0; n < 8; ++n) a = (a ^ b) * 3;
        return a;
    case 5: {
        uint64_t v = 0;
        for (unsigned i = 8; i-- > 0;) v = v * 256 + in->data[i];
        return in->data[8] != 0 && v > b ? v + b : 0;
    }
    default: return 0;
    }
}
struct workload workload(unsigned kind)
{
    static const char *names[] = {"arithmetic","bitwise","conditional","branches","loop8","records"};
    struct workload w = {.name=names[kind],.kind=kind};
    const struct rc_integer_insn arithmetic[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_CONSTANT,.dst=2,.immediate=2},
        {.op=RC_INT_MUL,.dst=2,.left=1,.right=2},{.op=RC_INT_ADD,.right=2},{.op=RC_INT_RETURN}};
    const struct rc_integer_insn bitwise[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_AND,.dst=2,.right=1},
        {.op=RC_INT_OR,.dst=3,.right=1},{.op=RC_INT_ADD,.dst=2,.left=2,.right=3},
        {.op=RC_INT_SUB,.dst=3,.right=1},{.op=RC_INT_XOR,.left=2,.right=3},{.op=RC_INT_RETURN}};
    const struct rc_integer_insn conditional[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_ULT,.dst=2,.left=1},
        {.op=RC_INT_BRANCH,.left=2,.target=6},{.op=RC_INT_XOR,.right=1},{.op=RC_INT_RETURN},
        {.op=RC_INT_CONSTANT,.dst=2,.immediate=2},{.op=RC_INT_MUL,.dst=2,.left=1,.right=2},
        {.op=RC_INT_ADD,.right=2},{.op=RC_INT_RETURN}};
    const struct rc_integer_insn loop[] = {{.op=RC_INT_ARGUMENT},
        {.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},{.op=RC_INT_CONSTANT,.dst=2,.immediate=8},
        {.op=RC_INT_CONSTANT,.dst=3,.immediate=1},{.op=RC_INT_CONSTANT,.dst=4,.immediate=3},
        {.op=RC_INT_XOR,.right=1},{.op=RC_INT_MUL,.right=4},{.op=RC_INT_SUB,.dst=2,.left=2,.right=3},
        {.op=RC_INT_BRANCH,.left=2,.target=5},{.op=RC_INT_RETURN}};
    const struct rc_integer_insn records[] = {{.op=RC_INT_ARGUMENT,.dst=1,.immediate=1},
        {.op=RC_INT_CONSTANT,.dst=2,.immediate=8},{.op=RC_INT_LOAD_U8,.dst=3,.left=2},
        {.op=RC_INT_BRANCH,.left=3,.target=6},{.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},
        {.op=RC_INT_CONSTANT,.dst=2},{.op=RC_INT_LOAD_U64,.left=2},
        {.op=RC_INT_ULT,.dst=3,.left=1},{.op=RC_INT_BRANCH,.left=3,.target=12},
        {.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},{.op=RC_INT_ADD,.right=1},{.op=RC_INT_RETURN}};
    const struct rc_integer_insn *source = NULL;
    switch (kind) {
    case 0: source=arithmetic; w.count=sizeof(arithmetic)/sizeof(*arithmetic); break;
    case 1: source=bitwise; w.count=sizeof(bitwise)/sizeof(*bitwise); break;
    case 2: source=conditional; w.count=sizeof(conditional)/sizeof(*conditional); break;
    case 4: source=loop; w.count=sizeof(loop)/sizeof(*loop); break;
    case 5: source=records; w.count=sizeof(records)/sizeof(*records); break;
    case 3:
        w.insns[0]=(struct rc_integer_insn){.op=RC_INT_ARGUMENT};
        w.insns[1]=(struct rc_integer_insn){.op=RC_INT_ARGUMENT,.dst=1,.immediate=1};
        for (uint32_t n=0; n<16; ++n) {
            uint32_t pc=2+3*n;
            w.insns[pc]=(struct rc_integer_insn){.op=RC_INT_ULE,.dst=2,.left=1};
            w.insns[pc+1]=(struct rc_integer_insn){.op=RC_INT_BRANCH,.left=2,.target=pc+3};
            w.insns[pc+2]=(struct rc_integer_insn){.op=RC_INT_ADD,.right=1};
        }
        w.insns[50]=(struct rc_integer_insn){.op=RC_INT_RETURN}; w.count=51; break;
    }
    if (source != NULL) memcpy(w.insns,source,w.count*sizeof(*source));
    return w;
}
