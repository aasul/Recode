#include "internal.h"

static bool fits_signed32(uint64_t value)
{
    return value <= INT32_MAX || value >= UINT64_MAX - (uint64_t)INT32_MAX;
}

static bool zf_live_after(const struct rc_program *program, const struct rc_cfg *cfg,
                           size_t block, size_t position)
{
    const struct rc_block *b = &cfg->blocks[block];
    for (size_t i = position; i < b->end; ++i) {
        enum rc_op op = program->insns[i].op;
        if (op == RC_JZ || op == RC_JNZ)
            return true;
        if (rc_writes_zf(op))
            return false;
    }
    for (size_t s = 0; s < b->successor_count; ++s)
        if (cfg->blocks[b->successors[s]].zf_live)
            return true;
    return false;
}

enum rc_status rc_optimize(struct rc_program *program, struct rc_error *error)
{
    struct rc_cfg cfg = {0};
    enum rc_status status = rc_analyze(program, &cfg, error);
    if (status != RC_OK)
        return status;
    bool changed;
    do {
        changed = false;
        for (size_t b = cfg.count; b-- > 0;) {
            bool live = zf_live_after(program, &cfg, b, cfg.blocks[b].first);
            if (live != cfg.blocks[b].zf_live) {
                cfg.blocks[b].zf_live = live;
                changed = true;
            }
        }
    } while (changed);
    /* Analysis is complete; reuse the instruction map for compacted indexes. */
    size_t output = 0;
    for (size_t b = 0; b < cfg.count; ++b) {
        size_t i = cfg.blocks[b].first;
        while (i < cfg.blocks[b].end) {
            enum rc_op op = program->insns[i].op;
            cfg.block_for[i] = output;
            if (op == RC_NOP) {
                ++i;
                continue;
            }
            if (op != RC_ADD && op != RC_SUB) {
                program->insns[output++] = program->insns[i++];
                continue;
            }
            size_t end = i;
            uint64_t delta = 0;
            while (end < cfg.blocks[b].end) {
                struct rc_insn insn = program->insns[end];
                if (insn.op == RC_ADD)
                    delta += rc_extend(insn.immediate);
                else if (insn.op == RC_SUB)
                    delta -= rc_extend(insn.immediate);
                else if (insn.op != RC_NOP)
                    break;
                ++end;
            }
            bool live = zf_live_after(program, &cfg, b, end);
            /* ADD/SUB set ZF from the final RAX, so a representable nonzero
             * delta preserves ZF too. A zero delta still needs ADD 0 if live. */
            if (fits_signed32(delta) || fits_signed32(UINT64_C(0) - delta)) {
                size_t replacement = output;
                for (size_t j = i; j < end; ++j)
                    cfg.block_for[j] = replacement;
                if (delta != 0 || live) {
                    bool add = fits_signed32(delta);
                    program->insns[output++] = (struct rc_insn){add ? RC_ADD : RC_SUB,
                        (uint32_t)(add ? delta : UINT64_C(0) - delta)};
                }
                i = end;
            } else {
                while (i < end) {
                    cfg.block_for[i] = output;
                    if (program->insns[i].op != RC_NOP)
                        program->insns[output++] = program->insns[i];
                    ++i;
                }
            }
        }
    }
    for (size_t i = 0; i < output; ++i)
        if (rc_is_branch(program->insns[i].op))
            program->insns[i].immediate = (uint32_t)cfg.block_for[program->insns[i].immediate];
    program->count = output;
    rc_cfg_free(&cfg);
    return rc_fail(error, RC_OK, 0, 0);
}
