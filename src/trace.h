#ifndef RECODE_TRACE_H
#define RECODE_TRACE_H

#include "internal.h"

enum { RC_TRACE_MAX_OPS = 4096, RC_TRACE_MAX_CANDIDATES = 16, RC_TRACE_QUANTUM = 4096 };
/* State is always at the boundary before pc. A guard snapshot is after its
 * branch, before the successor. MOV and trace bookkeeping must preserve ZF. */
struct rc_state {
    uint64_t rax, argument;
    size_t pc;
    uint8_t zf;
    uint64_t remaining;
};
enum rc_trace_stop { RC_TRACE_RECORDING, RC_TRACE_LOOP, RC_TRACE_RETURN,
                     RC_TRACE_LIMIT, RC_TRACE_ABORT };
enum rc_trace_exit { RC_TRACE_SIDE_EXIT, RC_TRACE_YIELD, RC_TRACE_RETURNED };
struct rc_trace_op {
    struct rc_insn insn;
    uint32_t pc, next, exit_pc;
    bool taken;
};
struct rc_trace {
    struct rc_trace_op *ops;
    size_t count, capacity;
    uint32_t header;
    struct rc_state entry; /* Diagnostic observation, never a specialization. */
    enum rc_trace_stop stop;
};
typedef enum rc_trace_exit (*rc_trace_function)(struct rc_state *);
struct rc_trace_view {
    uint32_t header;
    rc_trace_function function;
};
struct rc_observer {
    void *context;
    struct rc_trace *recording;
    /* before may replace state through native execution; NULL disables observation. */
    bool (*before)(struct rc_observer *, struct rc_state *);
    void (*after)(struct rc_observer *, size_t, struct rc_state *, bool);
};
void rc_trace_record(struct rc_trace *trace, const struct rc_program *program,
                     size_t pc, const struct rc_state *state, bool returned);
enum rc_status rc_trace_validate(const struct rc_program *program,
                                  const struct rc_trace *trace, struct rc_error *error);
enum rc_status rc_trace_emit(const struct rc_program *program, const struct rc_trace *trace,
                              bool optimize, uint8_t *bytes, size_t capacity,
                              size_t *length, struct rc_error *error);
enum rc_status rc_trace_map(const struct rc_program *program, const struct rc_trace *trace,
                             bool optimize, size_t mapped, struct rc_code *code,
                             rc_trace_function *function, bool *allocated, struct rc_error *error);
void rc_interpret_observed(const struct rc_program *program, struct rc_state *state,
                            struct rc_observer *observer);

#endif
