# v1.6 performance engineering

## Baseline and method

The baseline is merged main 9f49d55296614cadeaadd7338b60bfac642a53fb,
which identifies as 1.5.0. At the initial check there was no v1.5 tag and the latest published release
was v1.0.0. A v1.5.0 tag/release appeared during this work and identifies this
same baseline; the final comparison uses it. No release was created for this work.

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

## Batch sampling fast path

PR 3 groups native records between the existing sample positions. It computes
the next sample once per run, omits clock/modulo/saturating-counter decisions for
that run, reuses the validated arity and writes completed at run boundaries.
Sampled records retain the original timing path. Memory entries still stop at
the first failing load; no later arguments/data are fetched by the execution
loop. Preflight, alias restrictions, lookup/tier selection, interpreter fallback,
mutex boundaries and the full-batch native pin are unchanged. There is no new
public interface, extra allocation, executable code or global execution lock.

Native-loop fusion was considered against this C-loop prototype. A fused body
would need separate batch register ownership, callee-saved loop state, memory
failure exits and returns at every existing sample boundary. This work retains
per-record native entry rather than introduce that additional native ABI. No
fused-body prototype was integrated or measured, so no performance claim about
fusion or SIMD follows. The simpler measured improvement does not remove all
validation, descriptor or native-entry overhead.

Immutable comparison: bc84cd2 versus 19cade9, seven alternating processes.
Median public batch ns/record, before/after:

| Program | 1 record | 128 records | 65536 records |
| --- | ---: | ---: | ---: |
| arithmetic | 78.651/77.350 | 19.847/15.276 | 20.164/15.929 |
| bitwise | 71.985/78.231 | 20.877/15.572 | 20.196/19.389 |
| conditional | 74.556/75.117 | 20.155/15.782 | 21.025/17.708 |
| branches | 86.283/88.798 | 31.060/27.779 | 35.092/29.608 |
| loop8 | 83.390/84.789 | 26.260/23.323 | 28.325/25.028 |
| records | 71.099/78.296 | 22.666/19.028 | 22.052/26.205 |

At 128 records the arithmetic and bitwise medians improve about 23% and 25%
relative to the register-allocation milestone; branch-heavy and loop gains are
smaller. These are public API results. Seven raw samples per cell, all intermediate
sizes and scalar/kernel cells remain in profiling/results/batch. The separate
three-round thread diagnostic uses CPUs 0..3 and runs before then after; its order
is not alternating, so it is supporting evidence rather than a controlled claim
of scaling improvements.

GCC Release passed 14 CTests. Six affected GCC ASan/UBSan suites passed, and five
Release/sanitizer suites passed again after eliminating a redundant sampled-path
modulo. Linker wrapping still observes exactly 1250 clock reads and two locks per
10000-record warm FIXED batch, with no allocations. New tests check sample counts
for periods 1, 2, 3, 7, 16, 17 and UINT64_MAX, first-eight sampling, saturated
invocation arithmetic, and faults at record 0/7/8/15/16/17/127/128/255/256.
Successful prefixes, untouched failed/later slots and attempted/jitted counts are
asserted. Existing guard pages, validation, ownership, cache pressure, concurrent
registration/invalidation/clearing and mapping failure tests remain enabled.
Clang analysis and post-commit fork scan/next found no confirmed new defect.

## Compilation pipeline

PR 4 prepares one validated layout per managed integer compilation and reuses it
for publication. The private plan borrows the runtime's immutable owned program
while the runtime mutex protects translation lifetime. It contains bounded scratch
allocation/offset arrays, not persistent metadata or executable caller pointers.
Standalone private emitter/map entry points still validate their program inputs.
The prepared mapping path checks capacity before mmap and emits the exact planned
layout into RW storage before mprotect makes it RX. Publication failure keeps the
existing rollback/retained-mapping accounting. No shared executable arena, RWX
mapping, metadata cache or policy/tier change is introduced.

A straight-line program with one final return needs only a definition bitset,
not a heap-allocated CFG fixed point. Structural fields, fallthrough, memory flags
and register reads remain checked. Any branch or early return uses the existing
reachability/must-definition analysis. Preparation and publication share the same
register-read facts. The 37152-byte maximum plan is temporary C stack storage;
it replaces the emitter's existing bounded arrays, with no persistent-memory
increase. Page-isolated native mappings still round tiny functions to 4096 bytes.
An arena would require a separate W^X/lifetime/eviction design and was not added.

New tests compare standalone emitted bytes with prepared published bytes, check
undersized prepared-map rejection before allocation and execute through ABI
sentinels. The simple compilation probe asserts zero heap allocations, one mmap
and one mprotect. CFG allocation sweeps retain their real allocation failure path;
all such failures now assert zero mapping attempts. System-call rollback failure
coverage remains. GCC Release and GCC ASan/UBSan each passed all 14 CTests.
Clang's emitter analyzer reports no warning. Its CFG fact warning also reproduces
on immutable v1.5: the diagnostic stops unrolling the complete initialization
loop before following an arbitrary validated edge. Every target is in range and
all p->count facts are initialized first; this is not a confirmed defect. No
extra initialization or suppressed diagnostic was added to improve tool output.

Immutable compiler comparison: 7118731 versus f0bdafc. Median ns/operation
across seven processes (each compilation-stage sample averages 100 operations):

| Program | Validate before/after | Query before/after | Emit before/after | Managed compilation before/after |
| --- | ---: | ---: | ---: | ---: |
| arithmetic | 120.64/52.97 | 292.59/223.88 | 358.61/281.14 | 6175.91/5761.31 |
| bitwise | 143.67/58.95 | 354.74/273.33 | 450.58/373.82 | 5523.46/5234.62 |
| conditional | 192.76/194.74 | 414.44/411.92 | 538.38/536.04 | 5874.29/5597.76 |
| branches | 930.30/1008.54 | 1771.29/1773.11 | 2320.47/2401.75 | 9511.67/7548.51 |
| loop8 | 187.50/192.00 | 422.08/437.30 | 528.82/540.84 | 5898.91/5691.13 |
| records | 287.00/289.37 | 585.30/575.43 | 898.18/917.47 | 6939.78/6510.72 |

Managed compilation uses the runtime's compilation_ns boundary for 100 fresh
registrations, with construction/registration/execution/cleanup outside that
counter. Both libraries use the same current harness. This is separate from the
map/emit/publication/free interval. The probe confirms two temporary allocations
before and zero after for straight-line compilation, with one mmap/mprotect and
no munmap during successful compilation in both. Counter measurements, all
execution cells and pressure diagnostics are retained in profiling/results/compiler.
The single pressure diagnostic retains a 65536-byte executable peak for 4096
functions and identical persistent program/metadata ledgers; it is not an RSS
measurement or a seven-sample claim of lifecycle improvement. Native text is
unchanged from PR 2. No new quality finding was confirmed by the post-commit fork
scan/next. Its incomplete C coverage/import heuristics remain a limitation.

## Final v1.5 versus v1.6 candidate

Baseline: v1.5.0, 9f49d55296614cadeaadd7338b60bfac642a53fb.
Candidate implementation/package: eeb68033b0cdaeeae53dce69e21ea59fb202ff29.
Both were rebuilt from independent immutable archives. The C harness is identical
for both; later release-branch commits add results/documentation only. LLVM
23.1.4 and MIR dc2c763f1c8bc746102ce87933d866335f9e6c06 remain optional adapters.
Intel N150, GCC 16.2.1 -O3 -DNDEBUG, Linux 7.3.0-4-cachyos, glibc 2.44.

Scalar/cold/lifecycle and bulk studies use seven alternating library processes
and alternating engine order, CPU 0, with no overlapping local builds/tests.
Inputs, checksums and exact cold invocation accounting use the existing checked
comparison harness. Bulk forms inputs outside timing and stores every output.
Scalar includes input formation and checksum work. Pure bulk items have NULL
views; the private profile instead supplies views even for pure programs. These
are distinct public-call contexts, not interchangeable overhead estimates.
All observations and environmental records are in
[results/final](../benchmarks/profiling/results/final/). Frequency and background
activity remain uncontrolled. Summary quartiles use inclusive interpolation;
seven samples do not establish a universal ordering of close results.

Public scalar FIXED at 100000 calls, median ns/evaluation; cold compilation is
one managed compilation in that fresh process, us. The repeated 100-compilation
stage probe above warms compiler paths and is a different experiment.

| Program | Scalar v1.5/v1.6 ns | Compilation v1.5/v1.6 us | Kernel v1.5/v1.6 ns |
| --- | ---: | ---: | ---: |
| arithmetic | 59.943/58.927 | 19.791/16.458 | 4.290/3.886 |
| bitwise | 60.841/59.718 | 17.701/16.443 | 4.718/4.292 |
| conditional | 65.069/65.542 | 22.667/18.447 | 5.132/4.567 |
| branches | 72.068/70.179 | 21.181/22.123 | 16.887/15.368 |
| loop8 | 69.220/65.649 | 19.351/18.695 | 18.598/10.172 |
| records | 66.695/66.175 | 18.581/20.000 | 7.786/6.948 |

Scalar changes are small, including a conditional regression. Compilation improves
in four cells and regresses about 4% for branches and 8% for records. Neither
regression exceeds the provisional 15% investigation threshold here; raw ranges
remain visible. The loop kernel improves about 45%; other kernel gains are
smaller. Interpretation is unchanged by the compiler optimization and should not
be credited with noisy timing differences.

Warm public bulk, 128 records, median ns/record. These compare the same preformed
inputs/output writes across engines; C is the suite's independent reference, not
an assertion of the best possible handwritten C. External bounds-failure sentinels
are not Recode's error protocol; these measurements use valid views only.

| Program | v1.5 FIXED | v1.6 FIXED | Change | C | LLVM | MIR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 19.040 | 14.599 | -23.3% | 4.317 | 3.599 | 4.638 |
| bitwise | 20.186 | 13.728 | -32.0% | 4.964 | 3.700 | 4.591 |
| conditional | 19.780 | 14.063 | -28.9% | 4.676 | 3.601 | 5.532 |
| branches | 30.980 | 24.552 | -20.7% | 5.278 | 15.565 | 14.436 |
| loop8 | 30.920 | 20.971 | -32.2% | 6.060 | 4.128 | 11.425 |
| records | 23.128 | 20.576 | -11.0% | 7.836 | 4.051 | 7.394 |

All six 128-record batch medians improve, by about 11..32%. Recode remains slower
than the external native adapters here. Arithmetic spans 17.606..20.901 ns/record
before and 12.442..15.678 after. This is not a comparison against the older
published v1.5 point estimate, which came from another collection.

Arithmetic batching across all requested sizes, median ns/record:

| Items | v1.5 | v1.6 |
| --- | ---: | ---: |
| 1 | 73.249 | 75.754 |
| 8 | 25.966 | 21.641 |
| 32 | 22.277 | 14.849 |
| 128 | 19.040 | 14.599 |
| 1024 | 17.539 | 11.110 |
| 4096 | 17.331 | 11.971 |
| 65536 | 18.230 | 11.574 |

One-item arithmetic batches regress about 3%. A 128-item arithmetic batch takes
about 1869 ns at its median, about 68.5 million records/s. At 1024 items it takes
about 11377 ns, about 90.0 million records/s. These are derived from public batch
times, not kernel throughput. All other workload/size cells and per-process RSS
are retained, including cold batches and ADAPTIVE fallback/promotion counts.

Cold arithmetic end-to-end median us for exactly N calls (setup through calls,
cleanup excluded), final candidate:

| N | FIXED | INTERPRET | ADAPTIVE | LLVM | MIR |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 34.452 | 42.048 | 36.830 | 6749.017 | 649.872 |
| 10 | 49.381 | 30.222 | 62.809 | 6519.904 | 635.053 |
| 100 | 56.493 | 38.939 | 74.277 | 6455.732 | 625.408 |
| 1000 | 109.969 | 112.124 | 114.647 | 6377.847 | 634.600 |
| 10000 | 693.724 | 886.624 | 702.298 | 6488.705 | 740.957 |
| 100000 | 6067.881 | 8678.087 | 6083.812 | 7368.880 | 1593.095 |

The one-call FIXED case has no cold compilation. ADAPTIVE can remain interpreted;
there is no universal payback threshold. Sixty-four short-lived FIXED contexts
regress from 762.510 to 786.917 us in this collection, about 3%. Cleanup and total
cost remain in the raw lifecycle data. Warm gains do not justify compiling every
short-lived function.

Arithmetic scalar-process VmHWM medians are 2096/2100 KiB for v1.5/v1.6 FIXED.
These OS RSS values include process/compiler/runtime overhead, unlike Recode's
executable ledger. Large-batch caller arrays can dominate RSS. Both versions
still require a 4096-byte mapping per tiny function. The 4096-function pressure
study retains the same 65536-byte executable peak, 1999200 metadata bytes and
688128 program bytes. Pressure ledgers are not pressure RSS measurements.

Final disassembly retains arithmetic 81 -> 27 bytes, 16 -> 8 instructions and
4/5 -> 0/0 explicit stack loads/stores. The first five fixtures eliminate all
explicit stack traffic. Records is 275 -> 248 bytes and 60 -> 61 instructions,
with stack loads/stores 4/9 -> 4/3. Fewer instructions are not the acceptance test:
both actual execution and error/ABI behavior are measured. GNU as independently
assembles the arithmetic sequence to exactly the emitted 27 bytes; both SHA-256
values are e829d5318170b3790ac9979f9a87b87ba08b781c9ece4fb89bcd424943e60661.
Its input is retained in final/verification/arithmetic.s.

Final reproduction uses the same commands, without overlapping builds and timing:

```sh
COMPARISON_LLVM=ON COMPARISON_MIR=ON MIR_INCLUDE_DIR=/tmp/recode-mir \
MIR_LIBRARY=/tmp/recode-mir-build/libmir_static.a \
MIR_REVISION=dc2c763f1c8bc746102ce87933d866335f9e6c06 BENCH_ROUNDS=7 \
  sh benchmarks/competitors/compare-revisions.sh v1.5.0 eeb6803 /tmp/recode-final
sh benchmarks/profiling/compare.sh v1.5.0 eeb6803 /tmp/recode-final-profile
```

For each round 1..7, alternate before/after order and run collect.py on each
comparison directory with --bulk-only --rounds 1 --order-offset ROUND_MINUS_ONE
--cpu 0 --revision EXACT_LIBRARY_SHA --harness-revision eeb6803. The collector
uses all seven sizes and alternates engine order. The final environment manifest
retains each invocation's metadata and exact dependency revisions.

### Thread study and regressions

The batch diagnostic uses CPUs 0..3, seven alternating library processes and
three internal rounds per process. Each thread checks 32768 records in batches
of 128 on a shared runtime. Times include thread creation/join and output checks;
21 cells per configuration are clustered in seven processes. A repeat was run
because the initial multi-thread ordering was unfavorable and noisy. Both raw
collections are retained; none was discarded.

| Threads | Initial v1.5/v1.6 ns/record | Repeat v1.5/v1.6 ns/record |
| --- | ---: | ---: |
| 1 | 22.283/14.984 | 22.072/16.323 |
| 2 | 12.278/13.378 | 14.606/13.054 |
| 4 | 10.783/11.365 | 11.803/11.117 |

The initial two/four-thread medians regress about 9%/5%; their ordering reverses
in the repeat. No stable multi-thread improvement is established. Lock counts
remain two per batch. Shorter work outside the mutex can shift contention and
handoff costs, but this experiment does not isolate that cause. Strong single-thread
gains do not imply proportional multi-core scaling. The separate scalar scaling
collector uses CPUs 0..3 and seven rounds per library but runs all baseline rounds
before all candidate rounds; it is supporting evidence with that order limitation.
The scalar/cold study's additional concurrent rows are pinned to CPU 0, so they
measure same-core contention rather than multi-core scaling.

The retained trade-offs include one-item arithmetic batching, conditional scalar
latency, branch/record compilation, short-lived context cost, noisy concurrent
medians, page-rounding overhead and a remaining LLVM/MIR warm-throughput gap.
The integrated changes have practical batch/kernel benefits without a general
claim that every workload or execution context improves.
