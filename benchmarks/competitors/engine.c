#include "bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct engine {
    unsigned mode;
    struct rc_runtime *runtime;
    struct rc_integer_program *program;
    rc_handle handle;
    void *external;
    entry_fn native;
    unsigned kind;
};
void require(enum rc_status status)
{
    if (status != RC_OK) { fprintf(stderr,"comparison: %s\n",rc_status_string(status)); exit(1); }
}
struct engine *engine_open(const char *mode, struct metrics *m)
{
    uint64_t start=bench_now();
    struct engine *e=calloc(1,sizeof(*e)); if (e == NULL) exit(1);
    const char *names[]={"c","interpret","fixed","adaptive","llvm","mir"};
    for (e->mode=0; e->mode<6; ++e->mode) if (strcmp(mode,names[e->mode]) == 0) break;
    if (e->mode == 6) exit(1);
    if (e->mode >= 1 && e->mode <= 3) {
        struct rc_runtime_config c=rc_runtime_default_config();
        c.policy=e->mode == 1 ? RC_POLICY_INTERPRET : e->mode == 2 ? RC_POLICY_FIXED : RC_POLICY_ADAPTIVE;
        c.hot_threshold=2;
        require(rc_runtime_create(&c,&e->runtime,NULL));
    }
    if (e->mode == 4) {
#ifdef HAVE_LLVM
        e->external=llvm_open();
#else
        fprintf(stderr,"LLVM adapter not built\n"); exit(2);
#endif
    }
    if (e->mode == 5) {
#ifdef HAVE_MIR
        e->external=mir_open();
#else
        fprintf(stderr,"MIR adapter not built\n"); exit(2);
#endif
    }
    m->init_ns=bench_now()-start;
    m->code_bytes=-1; m->executable_bytes=-1;
    return e;
}
void engine_build(struct engine *e, const struct workload *w, struct metrics *m)
{
    e->kind=w->kind;
    if (e->mode == 0) return;
#ifdef HAVE_LLVM
    if (e->mode == 4) { e->native=llvm_build(e->external,w,m); return; }
#endif
#ifdef HAVE_MIR
    if (e->mode == 5) { e->native=mir_build(e->external,w,m); return; }
#endif
    uint64_t start=bench_now();
    struct rc_builder *b=NULL;
    require(rc_builder_create(8,2,&b,NULL));
    for (size_t n=0; n<w->count; ++n) require(rc_builder_append(b,w->insns[n],NULL));
    m->build_ns=bench_now()-start;
    start=bench_now(); require(rc_builder_finalize(b,&e->program,NULL));
    m->validate_ns=bench_now()-start; /* Includes the finalized immutable copy. */
    rc_builder_destroy(b);
    start=bench_now(); require(rc_runtime_register_integer(e->runtime,e->program,&e->handle,NULL));
    m->register_ns=bench_now()-start;
}
uint64_t engine_call(struct engine *e, const struct input *in)
{
    if (e->mode == 0) return reference(e->kind,in);
    if (e->native != NULL) return e->native(in->args[0],in->args[1],in->data,sizeof(in->data));
    struct rc_memory_view view={in->data,sizeof(in->data)}; uint64_t value;
    require(rc_runtime_call_integer_view(e->runtime,e->handle,in->args,2,&view,&value,NULL));
    return value;
}
void engine_stats(struct engine *e, struct metrics *m)
{
    if (e->runtime == NULL) return;
    struct rc_function_stats f; struct rc_runtime_stats r;
    require(rc_runtime_function_stats(e->runtime,e->handle,&f,NULL));
    require(rc_runtime_get_stats(e->runtime,&r,NULL));
    m->compile_ns=f.compilation_ns; m->code_bytes=(int64_t)f.generated_size;
    m->executable_bytes=(int64_t)r.peak_executable_bytes;
    m->interpreted=f.interpreted; m->jitted=f.jitted; m->evictions=r.evictions;
}
void engine_close(struct engine *e)
{
    if (e->runtime != NULL) require(rc_runtime_destroy(&e->runtime,NULL));
    rc_integer_program_destroy(e->program);
#ifdef HAVE_LLVM
    if (e->mode == 4) llvm_close(e->external);
#endif
#ifdef HAVE_MIR
    if (e->mode == 5) mir_close(e->external);
#endif
    free(e);
}
