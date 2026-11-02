# v1.6 performance engineering

## Baseline and method

The baseline is merged main 9f49d55296614cadeaadd7338b60bfac642a53fb,
which identifies as 1.5.0. There is no v1.5 tag; GitHub's latest published release
was v1.0.0 when checked. No release was created for this work.

The profiling harness uses the comparison suite's six programs and independent C
references. It checks every output after timing, forms inputs before timing, and
separates native kernels, public scalar calls, public batches and interpretation.
Batch sizes are 1, 8, 32, 128, 1024, 4096 and 65536. Each cell executes 131072
records. Seven fresh processes per library rotate engine order; revision order
alternates. Both baseline labels use identical immutable library code to expose
measurement variation. CPU affinity is pinned to the first available CPU;
frequency, thermal state and background activity remain uncontrolled. No local
builds or tests overlap collection. See the retained environment and raw samples
in [profiling/results/baseline](../benchmarks/profiling/results/baseline).

`instructions.csv` counts static disassembled instructions, explicit stack loads,
stack stores and branches. These are not dynamic operation counts or hardware
counter measurements. RET's implicit stack read is excluded. The loop body's
operations execute repeatedly. No perf executable is installed on this host.

Baseline medians at 128 records, ns/record:

| Program | Kernel | Public scalar | Public batch | Bytes | Instructions | Stack loads/stores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 4.314 | 60.664 | 21.986 | 81 | 16 | 4/5 |
| bitwise | 4.728 | 59.702 | 25.729 | 119 | 26 | 10/7 |
| conditional | 5.112 | 64.628 | 21.933 | 140 | 30 | 8/7 |
| branches | 16.896 | 69.483 | 34.788 | 850 | 199 | 65/34 |
| loop8 | 16.069 | 67.110 | 36.537 | 138 | 26 | 6/8 |
| records | 7.769 | 57.242 | 22.813 | 275 | 60 | 4/9 |

These timings differ from the published v1.5 table because this experiment uses
preformed inputs and a different call loop. Compare identical harnesses across
revisions, not these medians against unlike published boundaries.

Compilation probe medians, ns per operation:

| Program | Validate | Query including validate/layout | Emit including validate/layout | Map, emit, RX publication and free |
| --- | ---: | ---: | ---: | ---: |
| arithmetic | 124.81 | 166.93 | 202.96 | 7026.54 |
| branches | 934.80 | 1230.15 | 1600.91 | 7401.25 |
| records | 285.93 | 377.49 | 466.49 | 6231.33 |

Each stage repeats 100 times. Query and emission timings include their own
validation. Map timing includes emission, mmap, mprotect and munmap; it does not
isolate individual system calls or equal the runtime's compilation statistics.
Page-rounded executable use is 4096 bytes for each program, irrespective of text.

## Confirmed costs and next experiments

1. Public dispatch dominates tiny kernels. The linker-wrapped diagnostic observes
   20000 mutex acquisitions and 1250 clock reads per 10000 warm scalar FIXED calls,
   versus two mutex acquisitions and 1250 clock reads per 10000-record batch.
   Neither warm path allocates. Counts include sampling; wrapper timing is not
   used as latency evidence. Hash lookup/tier selection occur once per batch.
2. The integer emitter stores every write, including values immediately reloaded.
   RAX forwarding eliminates some left loads but never those stores. The arithmetic
   assembly has nine explicit stack memory operations for only two arithmetic ops.
3. Batch code computes a sampling modulo and saturating arithmetic for every
   record, even when no clock is read. It reloads descriptors and enters the native
   function for each item. Sampling semantics must survive any chunked fast path.
4. Validation/layout repeat in query and mapping. The stage probe establishes a
   measurable cost, especially for branch-heavy programs. Executable publication
   and cleanup are included in the larger map interval; no unsupported claim about
   individual syscall dominance follows from that aggregate.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
mkdir /tmp/recode-native
./build/recode_integer_profile /tmp/recode-native
objdump -D -b binary -m i386:x86-64 /tmp/recode-native/arithmetic.bin
./build/recode_dispatch_probe
sh benchmarks/profiling/compare.sh 9f49d55 HEAD /tmp/recode-profile-comparison
```

The comparison script builds archives of both immutable libraries, then compiles
the same current harness against each static library and private header set. It
uses seven rounds by default. Its output preserves raw timings, exact SHAs,
environment and disassembly. The harness is private tooling, not an installed API.
The existing optional LLVM/MIR collector retains cold, lifecycle, concurrent and
bulk comparisons; `BENCH_ROUNDS` now defaults to seven for compare-revisions.sh.
Normal library builds still require neither external compiler.

Baseline arithmetic across the seven process samples spans 4.270..7.882 ns/kernel,
55.369..68.083 ns/scalar and 20.230..26.519 ns/batch record at size 128. Retain
this variation when judging close improvements. The same baseline was rebuilt
independently and passed all 14 CTests. Its installed LLVM/MIR comparison suite
passed all 79 tests. Seven-round scalar/cold/concurrent and bulk measurements are
retained under baseline/scalar and baseline/bulk, with their own schemas.

The post-commit analyzer used the required fork at
5179f6ba61dca2050813a1aa7e2f71ba7e4ce765 in the existing isolated environment.
A full scan and next completed; jscpd failed, so duplication coverage is missing.
A second objective scan excluded only generated build trees and the tool venv.
C coupling/coverage heuristics report unused executables and untested declarations;
the profile is exercised against all six independent references, not a unit test
of a benchmark. The security candidate at competitors/main.c:36 is a false
positive: sscanf converts a bounded fgets buffer using %ld into a long, with no
string destination. No confirmed source defect was identified. Scores are not
correctness evidence. Historical generated-file findings from the first scan are
not release source findings. No project source was excluded from review.

## Register allocation design

The integer backend assigns the most frequently referenced virtual registers to
fixed caller-saved homes using a bounded use-count scan and at most seven
64-register selection scans. Non-memory programs can use RDX and R8..R11, plus
argument registers that are never read by retained ARGUMENT operations. Memory
entries reserve data/result/length/fault registers and use R11 and unused argument
registers. RAX and RCX remain scratch. Spills are deterministic, with authoritative
stack slots for unassigned virtuals. Frames disappear when no slots are needed.
There are no edge shuffles, callee-saved clobbers or extra CPU requirements.

A block-local allocator was considered, but flushing at every edge would retain
traffic in the branch and loop workloads. Fixed homes avoid that reconciliation
and any global liveness fixed point. This is not a next-use or global optimizing
allocator; high-pressure programs still spill. A small backward pass drops dead
pure writes inside a block, conservatively treating every register as live on
block exits. Loads remain even when their value is unused, retaining bounds faults.
The pass neither changes program objects nor propagates values across branches.
Constant materialization uses zero-extending 32-bit moves only when the complete
64-bit value fits. Commutative operations reuse a destination that aliases either
operand; other right-destination aliases and shifts preserve inputs in RCX.
SETcc uses explicit REX prefixes for SIL/DIL and high registers.

The first complete experiment exposed excessive setup cost from initializing
limit-sized scratch arrays. That implementation was revised to initialize only
used target entries and assign every used skip/offset entry before reading it.
The discarded timing experiment stays outside the proposed release. A Clang
analyzer candidate on the failure-offset array read was inspected: the query
always assigned it. Returning the failure offset directly makes this invariant
explicit and removes the diagnostic. The analyzer then reported no warnings.

Regression coverage expands to 600 randomized 256-instruction programs over
8/16/64 registers, 32 inputs each, against an independent operation evaluator,
interpretation and native ABI sentinels. Every binary operation also exercises
left/right/different destination aliases, equal sources and all shift counts.
A 64-live-register loop checks spills across backedges and both return paths.
The earlier exact-offset snapshot now independently decodes the branch's rel32
target and checks its constant, entry marker and smaller size. The capacity test
uses a live multiplication chain: overwritten constants now legitimately vanish,
so they no longer exercise multi-page compilation failure. The replacement asserts
that generated text exceeds the one-page budget before checking fallback.
GCC Release passed 14 tests; GCC ASan/UBSan passed 14, and four affected tests
passed again after scratch initialization changes. Leak detection was disabled.
Native code is checked by execution, not sanitizer instrumentation.

Final register-allocation comparison uses baseline 9f49d55 and implementation
92da324, seven alternating processes. Medians at batch size 128:

| Program | Kernel before/after ns | Scalar before/after ns | Batch before/after ns | Map+emit+publish+free before/after ns | Bytes before/after |
| --- | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 4.474/3.888 | 55.687/53.527 | 20.888/21.699 | 6529.230/6632.320 | 81/27 |
| bitwise | 4.713/4.289 | 57.679/55.465 | 21.907/19.467 | 6140.350/6094.790 | 119/41 |
| conditional | 5.267/4.573 | 60.545/54.535 | 20.546/19.987 | 6069.730/6285.810 | 140/57 |
| branches | 17.655/15.181 | 69.792/67.136 | 33.149/30.353 | 7642.250/8536.240 | 850/430 |
| loop8 | 15.472/10.760 | 69.940/61.776 | 33.551/26.445 | 6191.620/6621.600 | 138/51 |
| records | 7.808/6.950 | 58.487/57.719 | 22.362/22.331 | 6196.570/7027.860 | 275/248 |

The first five programs eliminate all explicit stack loads/stores. Arithmetic is
8 static instructions instead of 16; loop8 is 13 instead of 26. Records changes
from 60 to 61 static instructions while reducing stack stores from nine to three;
its kernel result is close and should not be presented as a strong gain. The
public API benefits are smaller than kernel gains. Raw ranges and all batch sizes
are retained in profiling/results/registers. These map intervals include cleanup,
not just compilation; final runtime compilation statistics are measured separately.
The repeated validation and layout overhead remains a compilation target for PR 4.
The post-commit fork scan/next and Clang static analysis identified no confirmed
new defect; known incomplete import/coverage heuristics remain as above.
