#ifndef BATCH_RULE_SPEC_H
#define BATCH_RULE_SPEC_H
#include "recode_integer.h"

static const struct rc_integer_insn rule_insns[]={{.op=RC_INT_ARGUMENT,.dst=1},
        {.op=RC_INT_CONSTANT,.immediate=8},{.op=RC_INT_LOAD_U8,.dst=2},
        {.op=RC_INT_BRANCH,.left=2,.target=6},{.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},
        {.op=RC_INT_CONSTANT},{.op=RC_INT_LOAD_U64},{.op=RC_INT_ULT,.dst=2,.left=1},
        {.op=RC_INT_BRANCH,.left=2,.target=12},{.op=RC_INT_CONSTANT},{.op=RC_INT_RETURN},
        {.op=RC_INT_SUB,.right=1},{.op=RC_INT_RETURN}};

#endif
