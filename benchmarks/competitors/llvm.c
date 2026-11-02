#include "bench.h"
#include <llvm-c/Analysis.h>
#include <llvm/Config/llvm-config.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Object.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct llvm_state { LLVMOrcLLJITRef jit; int64_t text_bytes; };
static void checked(LLVMErrorRef error)
{
    if (error != LLVMErrorSuccess) {
        char *message=LLVMGetErrorMessage(error); fprintf(stderr,"LLVM: %s\n",message);
        LLVMDisposeErrorMessage(message); exit(1);
    }
}
static LLVMErrorRef object_size(void *context, LLVMMemoryBufferRef *buffer)
{
    struct llvm_state *s=context; char *message=NULL;
    LLVMBinaryRef object=LLVMCreateBinary(*buffer,NULL,&message);
    if (object == NULL) { fprintf(stderr,"LLVM object: %s\n",message); LLVMDisposeMessage(message); exit(1); }
    LLVMSectionIteratorRef section=LLVMObjectFileCopySectionIterator(object);
    s->text_bytes=0;
    for (; !LLVMObjectFileIsSectionIteratorAtEnd(object,section); LLVMMoveToNextSection(section)) {
        const char *name=LLVMGetSectionName(section);
        if (name != NULL && strncmp(name,".text",5) == 0)
            s->text_bytes+=(int64_t)LLVMGetSectionSize(section);
    }
    LLVMDisposeSectionIterator(section); LLVMDisposeBinary(object);
    return LLVMErrorSuccess;
}
void *llvm_open(void)
{
    if (LLVMInitializeNativeTarget() || LLVMInitializeNativeAsmPrinter()) exit(1);
    struct llvm_state *s=calloc(1,sizeof(*s)); if (s == NULL) exit(1);
    checked(LLVMOrcCreateLLJIT(&s->jit,NULL));
    LLVMOrcObjectTransformLayerSetTransform(LLVMOrcLLJITGetObjTransformLayer(s->jit),object_size,s);
    return s;
}
entry_fn llvm_build(void *state, const struct workload *w, struct metrics *m)
{
    struct llvm_state *s=state; uint64_t start=bench_now();
#if LLVM_VERSION_MAJOR >= 23
    LLVMContextRef c=LLVMContextCreate();
    LLVMOrcThreadSafeContextRef safe=LLVMOrcCreateNewThreadSafeContextFromLLVMContext(c);
#else
    LLVMOrcThreadSafeContextRef safe=LLVMOrcCreateNewThreadSafeContext();
    LLVMContextRef c=LLVMOrcThreadSafeContextGetContext(safe);
#endif
    LLVMModuleRef module=LLVMModuleCreateWithNameInContext("comparison",c);
    LLVMSetTarget(module,LLVMOrcLLJITGetTripleString(s->jit));
    LLVMSetDataLayout(module,LLVMOrcLLJITGetDataLayoutStr(s->jit));
    LLVMTypeRef u64=LLVMInt64TypeInContext(c), pointer=LLVMPointerTypeInContext(c,0);
    LLVMTypeRef types[]={u64,u64,pointer,u64};
    LLVMValueRef fn=LLVMAddFunction(module,"evaluate",LLVMFunctionType(u64,types,4,0));
    LLVMBuilderRef b=LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry=LLVMAppendBasicBlockInContext(c,fn,"entry"), blocks[64];
    for (size_t n=0; n<w->count; ++n) blocks[n]=LLVMAppendBasicBlockInContext(c,fn,"");
    LLVMBasicBlockRef failure=LLVMAppendBasicBlockInContext(c,fn,"bounds");
    LLVMPositionBuilderAtEnd(b,entry); LLVMValueRef regs[8];
    for (size_t n=0; n<8; ++n) regs[n]=LLVMBuildAlloca(b,u64,"");
    LLVMBuildBr(b,blocks[0]);
    for (size_t n=0; n<w->count; ++n) {
        struct rc_integer_insn i=w->insns[n]; LLVMValueRef value=NULL, a=NULL, v=NULL;
        LLVMPositionBuilderAtEnd(b,blocks[n]);
        if ((i.op >= RC_INT_ADD && i.op <= RC_INT_SLE) || i.op == RC_INT_BRANCH ||
            i.op == RC_INT_RETURN || i.op >= RC_INT_LOAD_U8) a=LLVMBuildLoad2(b,u64,regs[i.left],"");
        if (i.op >= RC_INT_ADD && i.op <= RC_INT_SLE) v=LLVMBuildLoad2(b,u64,regs[i.right],"");
        switch (i.op) {
        case RC_INT_ARGUMENT: value=LLVMGetParam(fn,(unsigned)i.immediate); break;
        case RC_INT_CONSTANT: value=LLVMConstInt(u64,i.immediate,0); break;
        case RC_INT_ADD: value=LLVMBuildAdd(b,a,v,""); break;
        case RC_INT_SUB: value=LLVMBuildSub(b,a,v,""); break;
        case RC_INT_MUL: value=LLVMBuildMul(b,a,v,""); break;
        case RC_INT_AND: value=LLVMBuildAnd(b,a,v,""); break;
        case RC_INT_OR: value=LLVMBuildOr(b,a,v,""); break;
        case RC_INT_XOR: value=LLVMBuildXor(b,a,v,""); break;
        case RC_INT_ULT: value=LLVMBuildZExt(b,LLVMBuildICmp(b,LLVMIntULT,a,v,""),u64,""); break;
        case RC_INT_ULE: value=LLVMBuildZExt(b,LLVMBuildICmp(b,LLVMIntULE,a,v,""),u64,""); break;
        case RC_INT_BRANCH:
            LLVMBuildCondBr(b,LLVMBuildICmp(b,LLVMIntNE,a,LLVMConstInt(u64,0,0),""),blocks[i.target],blocks[n+1]); continue;
        case RC_INT_JUMP: LLVMBuildBr(b,blocks[i.target]); continue;
        case RC_INT_RETURN: LLVMBuildRet(b,a); continue;
        case RC_INT_LOAD_U8:
        case RC_INT_LOAD_U64: {
            uint64_t width=i.op == RC_INT_LOAD_U8 ? 1 : 8;
            LLVMValueRef length=LLVMGetParam(fn,3);
            LLVMValueRef valid=LLVMBuildAnd(b,LLVMBuildICmp(b,LLVMIntULE,a,length,""),
                LLVMBuildICmp(b,LLVMIntUGE,LLVMBuildSub(b,length,a,""),LLVMConstInt(u64,width,0),""),"");
            LLVMBasicBlockRef load=LLVMAppendBasicBlockInContext(c,fn,"");
            LLVMBuildCondBr(b,valid,load,failure); LLVMPositionBuilderAtEnd(b,load);
            LLVMValueRef address=LLVMBuildGEP2(b,LLVMInt8TypeInContext(c),LLVMGetParam(fn,2),&a,1,"");
            value=LLVMBuildLoad2(b,width == 1 ? LLVMInt8TypeInContext(c) : u64,address,"");
            LLVMSetAlignment(value,1); if (width == 1) value=LLVMBuildZExt(b,value,u64,""); break;
        }
        default: fprintf(stderr,"LLVM adapter: unsupported workload op\n"); exit(1);
        }
        LLVMBuildStore(b,value,regs[i.dst]); LLVMBuildBr(b,blocks[n+1]);
    }
    LLVMPositionBuilderAtEnd(b,failure); LLVMBuildRet(b,LLVMConstInt(u64,UINT64_MAX,0));
    LLVMDisposeBuilder(b); m->build_ns=bench_now()-start;
    start=bench_now(); char *message=NULL;
    if (LLVMVerifyModule(module,LLVMReturnStatusAction,&message)) { fprintf(stderr,"%s\n",message); exit(1); }
    LLVMDisposeMessage(message); m->validate_ns=bench_now()-start;
    start=bench_now(); LLVMPassBuilderOptionsRef options=LLVMCreatePassBuilderOptions();
    checked(LLVMRunPasses(module,"default<O2>",NULL,options)); LLVMDisposePassBuilderOptions(options);
    LLVMOrcThreadSafeModuleRef sm=LLVMOrcCreateNewThreadSafeModule(module,safe);
    LLVMOrcDisposeThreadSafeContext(safe);
    checked(LLVMOrcLLJITAddLLVMIRModule(s->jit,LLVMOrcLLJITGetMainJITDylib(s->jit),sm));
    LLVMOrcExecutorAddress address; checked(LLVMOrcLLJITLookup(s->jit,&address,"evaluate"));
    m->compile_ns=bench_now()-start; m->code_bytes=s->text_bytes;
    entry_fn result; _Static_assert(sizeof(result) == sizeof(address),"native pointer");
    memcpy(&result,&address,sizeof(result)); return result;
}
void llvm_close(void *state)
{
    struct llvm_state *s=state; checked(LLVMOrcDisposeLLJIT(s->jit)); free(s);
}
