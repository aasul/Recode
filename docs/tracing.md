# Native loop tracing in v0.4

Recode traces the accumulator instruction subset documented in
[translator.md](translator.md). This is a native tracing compiler for owned,
validated IR, with interpreter continuation at every guard exit. It does not
translate arbitrary applications or preserve registers other than the modeled
RAX, immutable argument and ZF.

## Selecting the tier

```c
struct rc_runtime_config config = rc_runtime_default_config();
config.policy = RC_POLICY_TRACE;
config.trace_hot_threshold = 32;
config.trace_max_instructions = 256;
config.trace_max_candidates = 4;
```

Registration and calls use the existing runtime APIs. The default remains
RC_POLICY_ADAPTIVE. INTERPRET, FIXED and ADAPTIVE retain their existing decisions;
TRACE starts in the interpreter, records hot loops, and uses native traces within
that interpreter. It deliberately does not compile whole functions. This makes
loop recording reachable even during a single long call. Failure or ineligibility
continues interpretation. `rc_compile` and `rc_interpret` retain their contracts.
The public configuration and statistics structs have grown; clients must rebuild.
There is no binary ABI compatibility promise.

`trace_record_only = true` records one path per eligible header without compiling.
It is a diagnostic baseline including observer and profiling overhead. It does not
record indefinitely. The three trace limits are required only for TRACE; maximum
supported values are 4096 operations and 16 candidates. Other configuration
validation, including nonzero retry_calls, remains in effect.

## Discovery and recording

Registration selects the first distinct destinations of backward or self-directed
IR branches, in instruction order, up to the candidate limit. This bounded set is
shared by aliases. Only an actually executed backward edge increments its header's
saturating frequency counter. A not-taken forward fallthrough does not count.
Once hot, the next iteration records in the existing interpreter's instruction
step loop. A single caller owns a candidate's recording; other callers continue
interpretation or use already published traces.

Each recorded operation contains the original operation and immediate, source IR
index, actual next index, and, for conditionals, observed direction and alternate
successor. This captures order through jumps and repeated blocks. It does not
copy the CFG into a different layout. Block transitions can be recovered from
these source/next indices and the function CFG. Original byte offsets are absent.

The recorder saves an entry boundary observation for diagnostics, including RAX,
ZF and argument. These values are never treated as constants by the compiler.
Recording finishes on the first return to the starting header or a RET. Reaching
the operation limit aborts the candidate permanently; no partial trace is compiled.
Allocation failure also disables that candidate. A completed buffer is compacted
when allocation permits, otherwise its original bounded capacity stays owned.
Persistent buffer allocations are included in metadata_bytes.

## Trace IR validation and optimization

Validation checks the whole original function, entry index, operation count,
operation/immediate identity, every recorded successor, guard polarity and alternate
successor, and the final loop or return boundary. Interior returns and an interior
visit to the entry header are rejected. A failed or incomplete trace cannot be
published. Internal emitter APIs require readable storage and runtime-produced
entry state; they are not a public native-code interface.

The native emitter removes recorded unconditional jumps because their successors
are adjacent along the trace. Optimization additionally removes NOPs and combines
ADD/SUB runs across NOPs and recorded unconditional edges when the modular delta
fits one sign-extended ADD or SUB immediate. Runs stop at guards, assignments,
comparisons, tests and returns. The last RAX and ZF remain identical. A zero delta
still emits ADD 0, since exit state includes ZF even when the eventual return does
not use it. Nonrepresentable deltas retain the original operations. Trace metadata
keeps the complete original mapping even when native instructions are combined.
No constant argument specialization, memory analysis or register allocator exists.

## Guards and boundary state

Consider the IR for `mov rax,rdi; sub rax,1; jnz loop; ret`:

```text
0 INPUT
1 SUB 1       <- trace header
2 JNZ 1       <- observed taken; alternate successor is 3
3 RET
```

After enough backward edges, the recorder observes positions 1,2, returning to 1.
The compiled trace decrements RAX directly and guards the recorded taken decision.
When RAX becomes zero, the guard returns a side exit with `rax=0`, `zf=1`, and
`pc=3`. The interpreter executes RET once. It does not repeat SUB or JNZ.
A trace observing a not-taken JZ instead guards ZF clear and exits to the original
target when ZF becomes set. Every recorded conditional has a guard, including
conditionals whose two successors happen to be the same index.

Internal rc_state holds RAX, the immutable argument, the next IR position, ZF,
and a native iteration quantum. Every state boundary is **before pc**. A failed
guard snapshots the state **after its conditional**, before the alternate
successor. A normal native RET snapshots after RET and returns an explicit
RETURNED reason; the interpreter then returns RAX without executing another IR
instruction. A cyclic trace yields at its header after 4096 complete iterations.
Yield is a scheduling boundary, not an instruction-budget error.

The native ABI receives rc_state* in RDI and returns SIDE_EXIT, YIELD or RETURNED
in EAX. RAX holds the accumulator, RSI the argument and RCX the remaining quantum.
Entry loads state and restores modeled ZF by comparing its byte with 1. At the
backedge, SETNZ saves !ZF in DL before DEC updates the iteration quantum. TEST
DL,DL restores exactly the architectural ZF on both the loop and yield paths. MOV,
guards and snapshot stores preserve it. Snapshot SETZ runs before writing the
exit index or result reason. Only caller-saved registers are used; the code makes
no calls and changes neither stack alignment nor callee-saved registers. Every
indirect entry begins with ENDBR64. Guard targets use checked bounded rel32
layout and separate exit stubs.

After an exit the same interpreter step implementation resumes from state.pc.
INPUT can reload the original argument even if the trace has changed RAX.
A side exit blocks re-entry to that candidate for the rest of the invocation,
preventing immediate guard-failure cycles. Once all candidates are blocked, the
interpreter drops observer callbacks for the remainder of that invocation. Yield permits subsequent entry after
one interpreted instruction. Eight short exits without a complete native loop
iteration impose a translation-call cooldown. Since v0.7, per-candidate exit
feedback is merged in completion order under the existing completion mutex,
removing a separate lock acquisition on each side exit. Cooldowns begin at that
completion; active calls retain their already acquired snapshots. Existing in-flight snapshots may
still use the code; no mapping is released by this profitability cooldown.

## Ownership, concurrency and limits

Each translation owns its candidate records, immutable completed trace IR, native
entries and mappings. Identity is the shared canonical function IR plus header;
there is at most one recorded path per selected header. No trace pointer escapes
the runtime. Different arguments reuse the same guards and dynamic entry state.
There are no side traces or chains between native traces.

The runtime mutex serializes candidate hotness, recording claims, compilation,
publication, accounting and cache changes. Recording operations and native code
execute outside it. A TRACE call pins its entire translation from lookup until
return, including interpreted portions, so its local native-entry snapshot stays
valid. This is more conservative than per-entry pins: eviction, invalidation and
clear return BUSY for that translation throughout the call. Handles also retain
active-call ownership. Aliases share pins and recording claims. Destroy retains
the v0.3 requirement to exclude new API entry and join threads before final free.

New mappings can be published while calls pin a translation because existing
mappings are never overwritten. Compilation is serialized and a recording claim
prevents duplicate work. Failed emission/protection never publishes an entry.
The single executable_bytes ledger includes whole-function code, every trace
mapping and failed rollback mappings. Allocation reserves page-rounded capacity
before mmap. All mappings transition RW to RX; there is no RWX interval.

LRU ordering is per translation. Eviction releases all its unpinned mappings and
retains bounded trace IR for later compilation. No pinned translation is a victim,
even if only one of its traces is currently useful. Failed release disables the
entry, retains ownership and accounting, and is retried by clear, invalidation,
removal, destruction or eviction. A retained failed mapping is not overwritten
by another compilation. Last-reference removal frees trace IR and candidates only
after successful code release. Failed cleanup retains the handle for retry.

Compilation failure permits at most three attempts per candidate, with retry_calls
accepted calls to that translation between attempts. Unrelated traffic cannot
advance it. Eviction and invalidation also impose that cooldown. Successful
compilation does not erase a candidate's accumulated failure count. Saturated
cooldowns remain ineligible. Recording limit failures are never retried. Metadata
is bounded per translation by candidate and operation limits, but total registered
functions, IR and metadata are not subject to a global data-memory quota.

Handle and translation lookup use growing chained hash buckets. IR identity
always performs full field equality after matching the hash. Average lookup avoids
the former full list walk; adversarial collisions can still require linear work.
Doubly linked MRU/LRU updates take constant time. Eviction can scan pinned or empty
translations. Buckets are freed when the final handle is removed.

## Statistics and verification

Function invocation/interpreted/jitted counters retain their whole-call meanings:
a hybrid TRACE call counts as interpreted; jitted counts whole-function calls.
Compilation attempts, failures, time and last error include trace compilation.
The last successful emitted size is distinct from page-rounded mapped usage.
New per-handle counters report recordings, aborted candidates, recorded operations,
recording time, native entries, completed native loop iterations, side exits,
yields and short-exit cooldowns. Trace hits count available cached entries acquired
by a call or at a hot edge; misses count eligible recording/compilation lookups.
They are separate from the v0.3 whole-function cache-hit counters. A call merges
its local totals on completion, so concurrent snapshots can lag active work.
Recording time covers the observed path, including scheduling and completion-lock
wait, excluding compilation. Compilation time includes validation, emission,
capacity management and mapping. Neither is an estimate of total runtime overhead.

The trace suite directly checks source order, guard directions and exact RAX/ZF,
argument, pc and quantum snapshots. It includes 300 fixed-seed trace programs with
32 native snapshot comparisons each, optimization on/off, and bounded native
execution. The existing differential suite now adds TRACE to original machine
code, independent byte interpretation, public interpretation and both native
compilation modes. Its 1000 forward programs and 200 terminating countdown programs
run in child processes with alarms. Closed-loop trace execution is separately
killed by a subprocess timeout. Failure wrappers cover allocation, mmap, mprotect,
munmap, rollback ownership, cooldown, pins, removal and destruction. ThreadSanitizer
covers concurrent recording, publication, execution and cache operations. Generated
code is not sanitizer-instrumented; these checks do not prove race freedom.

See [tracing-verification.md](tracing-verification.md) for actual commands,
measurements, quality review and remaining performance limitations.

## Running benchmarks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build --parallel 2
timeout 60 ./build/recode_trace_bench
timeout 60 ./build/recode_trace_bench --pressure
./build/recode_runtime_bench --pressure
```

Five rounds rotate mode order and check every timed checksum. Workloads cover
countdown, a predictable branch with foldable arithmetic, a changing branch,
multiple exit paths, variable inputs and a short function with no loop. Counts
1, 32 and 20000 expose cold overhead. Modes include equivalent optimized C,
validated interpretation, optimized whole-function JIT, fixed runtime, record-only,
cold TRACE, warmed TRACE and a low-level trace kernel with interpreter completion.
The kernel starts immediately after INPUT and includes guard continuation; it
omits runtime lookup, profiling and ownership overhead. It is not a public API.

ns_per_call measures the call loop. total_us also includes setup, recording,
compilation and, for warmed TRACE, eight checked warmup calls. Cleanup and later
statistics queries are outside timing. Direct C may algebraically remove the
loop, so it is an equivalent-result baseline with different CPU work. Exit probes
include state setup, one native iteration, returned-reason and snapshot checks;
they do not isolate a single machine instruction. Pressure mode registers 4096
distinct loop functions with 1, 4 and 16 code pages and verifies accounting and
cleanup. Neither memory counter measures process RSS or allocator overhead.
