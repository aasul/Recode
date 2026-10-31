#ifndef RECODE_INTERNAL_H
#define RECODE_INTERNAL_H

#include "recode.h"

struct rc_block {
    size_t first, end; /* Half-open instruction range. */
    size_t successors[2];
    size_t successor_count;
    size_t next; /* Reachability queue link. */
    unsigned defined, produces;
    bool reachable, zf_live;
};

struct rc_cfg {
    struct rc_block *blocks;
    size_t *block_for;
    size_t count;
};

enum { RC_RAX_DEFINED = 1, RC_ZF_DEFINED = 2 };

static inline bool rc_is_branch(enum rc_op op)
{
    return op == RC_JMP || op == RC_JZ || op == RC_JNZ;
}

static inline bool rc_writes_zf(enum rc_op op)
{
    return op == RC_ADD || op == RC_SUB || op == RC_CMP || op == RC_TEST;
}

enum rc_status rc_analyze(const struct rc_program *program, struct rc_cfg *cfg,
                           struct rc_error *error);
void rc_cfg_free(struct rc_cfg *cfg);

static inline enum rc_status rc_fail(struct rc_error *error, enum rc_status status,
                                     size_t offset, int system_errno)
{
    if (error != NULL)
        *error = (struct rc_error){status, offset, system_errno};
    return status;
}

static inline uint64_t rc_saturating_add(uint64_t a, uint64_t b)
{
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}

static inline uint64_t rc_extend(uint32_t value)
{
    return (value & UINT32_C(0x80000000)) != 0
        ? UINT64_C(0xffffffff00000000) | value : value;
}

enum rc_status rc_interpret_validated(const struct rc_program *program, uint64_t argument,
                                       uint64_t budget, uint64_t *result);
enum rc_status rc_map_program(const struct rc_program *program, size_t needed,
                               struct rc_code *code, bool *allocated, struct rc_error *error);

#endif
