# Restricted translator contract

The input is a System V AMD64 function with signature `uint64_t f(uint64_t)`.
RDI contains the argument, RAX contains the returned value, and RET uses the
caller's return address. The supported instructions touch no memory except the
return stack access and modify no callee-saved registers. The contract preserves
the executed branches and returned value, including behavior of terminating
loops. It does not preserve final flags, timing, or full exception behavior.

The exact accepted encodings are listed in the README. MOV, ADD, SUB, CMP, TEST,
JMP, JZ/JNZ, NOP, and RET semantics follow the
[Intel SDM instruction reference](https://www.intel.com/content/www/us/en/content-details/835757/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-2a-2b-2c-and-2d-instruction-set-reference-a-z.html).
GNU as and objdump independently checked representative branch and flag encodings.

## IR and control flow

`rc_program` retains its flat, owned instruction array. Each instruction contains
an operation and raw 32-bit immediate. CONST zero-extends; ADD/SUB/CMP sign-extend.
JMP/JZ/JNZ use the immediate as an absolute instruction index. No instruction
retains an input pointer or an encoding-specific displacement. Programs and byte
buffers are limited to UINT32_MAX entries/bytes; allocation-size limits are also
checked before reading input or array storage.

The decoder reads the entire supplied buffer, records instruction offsets, then
resolves signed displacements relative to the end of each branch. It checks
addition/subtraction bounds and binary-searches instruction starts. A target
outside the buffer, at its end, or inside an instruction reports RC_BRANCH at the
branch byte offset. Every byte must decode, even in an unreachable region.
Truncated supported instructions report RC_TRUNCATED; unsupported forms report
RC_UNSUPPORTED.

`rc_analyze` builds a temporary internal CFG used by validation, optimization,
and emission. Block leaders are entry, branch targets, and instructions following
terminators. Each block has a half-open instruction range and zero, one, or two
explicit successors. A conditional has target and fallthrough edges, JMP has only
its target, and RET has none. Non-terminating block boundaries have an explicit
fallthrough edge. A map associates instruction indexes with blocks. Multiple
predecessors require no special-case representation; analyses propagate along
all incoming edges. The graph is rebuilt after edits, so no cached graph can
become stale.

A queue marks blocks reachable from entry. Every decoded block must be reachable
in this graph, regardless of whether a branch condition is feasible for a
particular argument. RC_TRAILING now denotes unreachable instructions, including
unreachable cycles and padding after RET. A final reachable block with a
fallthrough beyond the buffer reports RC_MISSING_RET. A closed loop without any
RET is accepted. Validation proves neither termination nor a bound on execution;
callers must establish termination for inputs they execute.

## Initialization and flags

The forward must analysis tracks two facts: RAX defined and ZF defined. Non-entry
blocks start with both facts, while entry starts with neither, reflecting the
external calling edge even when entry has backedges. Each block's output unions
its input with definitions produced by its instructions. Incoming facts are
intersected along every reachable edge until stable. Bits only drop during this
iteration, so cycles converge. A second walk checks reads in instruction order.
Definitions later in a block or on only a backedge cannot justify an earlier read.

MOV initializes RAX and preserves ZF. ADD/SUB read RAX, wrap modulo 2^64, and set
ZF when their result is zero. CMP reads RAX and sets ZF from equality with its
sign-extended immediate, without changing RAX. TEST reads RAX and sets ZF when
RAX is zero, without changing it. JZ/JNZ read ZF and preserve both facts. RET reads
RAX. An undefined read reports RC_UNINITIALIZED or RC_UNDEFINED_ZF. Only ZF has
in-function consumers in this subset. Other flags and final flags are not part
of the contract.

## Optimization

The optimizer validates before mutation. A backward may analysis computes ZF
liveness across successors to a fixed point. Conditional jumps generate a use;
ADD/SUB/CMP/TEST kill incoming liveness, and MOV/JMP/NOP preserve it. A loop can
therefore keep flags live across a backedge or a join.

Within each block, consecutive ADD/SUB operations, allowing NOPs, are summed in
`uint64_t` modulo 2^64. The sum must fit a sign-extended ADD imm32, or its modular
negation must fit SUB imm32. Otherwise the original arithmetic is retained.
Unsigned subtraction avoids negating INT32_MIN in signed arithmetic.

The final arithmetic operation sets ZF from final RAX. A nonzero folded delta
preserves both. A zero delta becomes ADD 0 if ZF is live after the run, even when
MOV subsequently replaces RAX; if ZF is dead it can be removed. Folding never
crosses a block boundary, even when a block appears to have one predecessor.
NOPs are removed, and compaction remaps every branch target. An empty fallthrough
block maps to the first surviving instruction after it. Instruction count never
increases. CMP/TEST and observable flag definitions are not deleted as dead
register writes.

## Emission and ownership

Emission validates, lays out instructions in block order, and assigns byte
offsets after the four-byte ENDBR64 entry marker. JMP uses E9 rel32; JZ/JNZ use
0F 84/85 rel32. Displacements are `target - end_of_branch`, checked against the
signed 32-bit range before any output is written. The second pass encodes and
patches each branch using those offsets. Every target is an emitted instruction
boundary. Canonical arithmetic uses accumulator imm32 forms. Short branch
relaxation is not implemented. Emitted code can exceed input size.

An undersized output is untouched and reports the required length. An
unrepresentable rel32 reports RC_CAPACITY without writing. Size queries also
build and validate the CFG and can fail allocation; they no longer promise to
allocate nothing. `rc_decode` and `rc_compile` require zero-initialized empty
outputs. A successful decode owns one instruction allocation, released with
`rc_program_free`. Temporary offset arrays and CFG allocations are released on
all paths. Borrowed IR arrays may be validated, optimized, and emitted but must
not be passed to `rc_program_free`. Counts must describe valid storage. Input,
output, and error objects must not overlap. Input bytes must remain readable and
unchanged during decoding.

Decode errors report byte offsets, including errors from CFG validation remapped
to source bytes. Direct IR errors report instruction indexes. Errors include a
status and system errno where applicable. Empty input reports RC_MISSING_RET.
Invalid IR targets or operations report RC_INVALID_IR.

Compilation allocates a private anonymous RW mapping, emits into it, then changes
it to RX with mprotect. There is no RWX mapping or writable alias. The allocation
length is retained separately from emission output so an emission-allocation
failure can roll back the full mapping. Linux rounds mapping lengths to pages;
x86-64 instruction/data caches are coherent. The function-pointer representation
is an explicit Linux x86-64 ABI dependency copied with memcpy.

If protection or emission fails and rollback munmap also fails, compilation
reports the cleanup errno and returns ownership of the non-executable mapping
with a null function pointer. Release it with `rc_code_free`. A failed release
retains ownership for retry. Do not copy owning objects, call after release, or
release during calls. Concurrent calls use no shared mutable runtime state;
mutation, compilation output, and release require caller synchronization.

## Compatibility and limits

Existing operations, struct layouts, function signatures, straight-line return
values, and arithmetic immediate semantics are retained. New operation/status
values are appended. Callers constructing IR must use instruction indexes for
branches and update indexes after their own edits. Previously rejected branch
encodings are now accepted, RET need not be final when later blocks are reachable,
and RC_TRAILING identifies the first unreachable instruction rather than the
preceding RET. Validation and size queries now allocate CFG storage and can
report RC_MEMORY.

The implementation has no ELF loader, memory operands, external/indirect edges,
call handling, cache, signal instrumentation, or termination enforcement. Analysis
uses finite fixed-point sweeps, which can take quadratic time on adversarial block
orders. The runtime is not a sandbox. Tests use child-process timeouts, but those
limits are not injected into emitted functions.
