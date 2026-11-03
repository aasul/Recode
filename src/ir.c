#include "internal.h"

#include <stdlib.h>

const char *rc_version_string(void)
{
    return RECODE_VERSION;
}

const char *rc_status_string(enum rc_status status)
{
    switch (status) {
    case RC_OK: return "success";
    case RC_ARGUMENT: return "invalid argument or nonempty output";
    case RC_MEMORY: return "allocation failed or size overflow";
    case RC_TRUNCATED: return "truncated instruction";
    case RC_UNSUPPORTED: return "unsupported encoding";
    case RC_UNINITIALIZED: return "value used before initialization";
    case RC_MISSING_RET: return "fallthrough beyond function bounds";
    case RC_TRAILING: return "unreachable instructions";
    case RC_INVALID_IR: return "invalid IR operation or branch target";
    case RC_CAPACITY: return "output buffer or relative branch capacity exceeded";
    case RC_SYSTEM: return "operating system error";
    case RC_BRANCH: return "branch outside input or into an instruction";
    case RC_BUSY: return "resource has active calls";
    case RC_NOT_FOUND: return "unknown function handle";
    case RC_BOUNDS: return "read outside memory view";
    case RC_BUDGET: return "diagnostic instruction budget exhausted";
    case RC_UNDEFINED_ZF: return "ZF used before initialization";
    }
    return "unknown error";
}

void rc_program_free(struct rc_program *program)
{
    if (program != NULL) {
        free(program->insns);
        *program = (struct rc_program){0};
    }
}

void rc_cfg_free(struct rc_cfg *cfg)
{
    free(cfg->blocks);
    free(cfg->block_for);
    *cfg = (struct rc_cfg){0};
}

static enum rc_status build_blocks(const struct rc_program *program, struct rc_cfg *cfg,
                                      struct rc_error *error)
{
    size_t count = program->count;
    for (size_t i = 0; i < count; ++i)
        cfg->block_for[i] = 0;
    for (size_t i = 0; i < count; ++i) {
        enum rc_op op = program->insns[i].op;
        if (op < RC_INPUT || op > RC_JNZ)
            return rc_fail(error,RC_INVALID_IR,i,0);
        if (rc_is_branch(op)) {
            size_t target = program->insns[i].immediate;
            if (target >= count)
                return rc_fail(error,RC_INVALID_IR,i,0);
            cfg->block_for[target] = 1;
        }
        if ((rc_is_branch(op) || op == RC_RET) && i + 1 < count)
            cfg->block_for[i + 1] = 1;
    }
    cfg->blocks[0] = (struct rc_block){.first = 0, .end = count};
    cfg->count = 1;
    cfg->block_for[0] = 0;
    for (size_t i = 1; i < count; ++i) {
        if (cfg->block_for[i] != 0) {
            cfg->blocks[cfg->count - 1].end = i;
            cfg->blocks[cfg->count++] = (struct rc_block){.first = i, .end = count};
        }
        cfg->block_for[i] = cfg->count - 1;
    }
    for (size_t b = 0; b < cfg->count; ++b) {
        struct rc_block *block = &cfg->blocks[b];
        struct rc_insn last = program->insns[block->end - 1];
        if (rc_is_branch(last.op))
            block->successors[block->successor_count++] = cfg->block_for[last.immediate];
        if (last.op != RC_JMP && last.op != RC_RET && block->end < count)
            block->successors[block->successor_count++] = b + 1;
        for (size_t i = block->first; i < block->end; ++i) {
            enum rc_op op = program->insns[i].op;
            if (op == RC_INPUT || op == RC_CONST)
                block->produces |= RC_RAX_DEFINED;
            if (rc_writes_zf(op))
                block->produces |= RC_ZF_DEFINED;
        }
        block->defined = RC_RAX_DEFINED | RC_ZF_DEFINED;
        block->next = SIZE_MAX;
    }
    return RC_OK;
}

static void propagate_definitions(struct rc_cfg *cfg)
{
    size_t head = 0, tail = 0;
    cfg->blocks[0].reachable = true;
    while (head != SIZE_MAX) {
        struct rc_block *block = &cfg->blocks[head];
        for (size_t s = 0; s < block->successor_count; ++s) {
            size_t successor = block->successors[s];
            if (!cfg->blocks[successor].reachable) {
                cfg->blocks[successor].reachable = true;
                cfg->blocks[tail].next = successor;
                tail = successor;
            }
        }
        head = block->next;
    }
    /* Must analysis starts at top, except the external entry edge. Each bit
     * only drops, so even cycles without a return reach a fixed point. */
    cfg->blocks[0].defined = 0;
    bool changed;
    do {
        changed = false;
        for (size_t b = 0; b < cfg->count; ++b) {
            struct rc_block *block = &cfg->blocks[b];
            if (!block->reachable)
                continue;
            unsigned output = block->defined | block->produces;
            for (size_t s = 0; s < block->successor_count; ++s) {
                struct rc_block *successor = &cfg->blocks[block->successors[s]];
                unsigned defined = successor->defined & output;
                if (defined != successor->defined) {
                    successor->defined = defined;
                    changed = true;
                }
            }
        }
    } while (changed);
}

static enum rc_status validate_block_entries(const struct rc_program *program,
                                                const struct rc_cfg *cfg, struct rc_error *error)
{
    for (size_t b = 0; b < cfg->count; ++b) {
        const struct rc_block *block = &cfg->blocks[b];
        if (!block->reachable)
            continue;
        unsigned defined = block->defined;
        for (size_t i = block->first; i < block->end; ++i) {
            enum rc_op op = program->insns[i].op;
            if ((rc_writes_zf(op) || op == RC_RET) && (defined & RC_RAX_DEFINED) == 0)
                return rc_fail(error,RC_UNINITIALIZED,i,0);
            if ((op == RC_JZ || op == RC_JNZ) && (defined & RC_ZF_DEFINED) == 0)
                return rc_fail(error,RC_UNDEFINED_ZF,i,0);
            if (op == RC_INPUT || op == RC_CONST)
                defined |= RC_RAX_DEFINED;
            if (rc_writes_zf(op))
                defined |= RC_ZF_DEFINED;
        }
        enum rc_op last = program->insns[block->end - 1].op;
        if (block->end == program->count && last != RC_JMP && last != RC_RET)
            return rc_fail(error,RC_MISSING_RET,program->count,0);
    }
    for (size_t b = 0; b < cfg->count; ++b) {
        if (!cfg->blocks[b].reachable)
            return rc_fail(error,RC_TRAILING,cfg->blocks[b].first,0);
    }
    return rc_fail(error, RC_OK, 0, 0);
}

enum rc_status rc_analyze(const struct rc_program *program, struct rc_cfg *cfg,
                           struct rc_error *error)
{
    if (program == NULL || (program->count != 0 && program->insns == NULL))
        return rc_fail(error, RC_ARGUMENT, 0, 0);
    size_t count = program->count;
    if (count == 0)
        return rc_fail(error, RC_MISSING_RET, 0, 0);
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(*cfg->blocks) ||
        count > SIZE_MAX / sizeof(*cfg->block_for))
        return rc_fail(error, RC_MEMORY, 0, 0);
    cfg->blocks = malloc(count * sizeof(*cfg->blocks));
    cfg->block_for = malloc(count * sizeof(*cfg->block_for));
    if (cfg->blocks == NULL || cfg->block_for == NULL) {
        rc_cfg_free(cfg);
        return rc_fail(error, RC_MEMORY, 0, 0);
    }
    enum rc_status status = build_blocks(program,cfg,error);
    if (status == RC_OK) {
        propagate_definitions(cfg);
        status = validate_block_entries(program,cfg,error);
    }
    if (status != RC_OK)
        rc_cfg_free(cfg);
    return status;
}

enum rc_status rc_validate(const struct rc_program *program, struct rc_error *error)
{
    struct rc_cfg cfg = {0};
    enum rc_status status = rc_analyze(program, &cfg, error);
    rc_cfg_free(&cfg);
    return status;
}
