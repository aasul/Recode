# v0.7 performance verification

This milestone removes unused policy clock reads, batches trace side-exit
feedback under the existing completion mutex, and replaces integer native NOP
padding with exact layout. It does not change the default ADAPTIVE policy,
thresholds, sampled execution accounting, generated trace snapshots or instruction
semantics. Recode remains experimental and slower than direct C in these tests.

## Environment and baselines

Intel N150, four cores, Linux 7.3.0-4-cachyos x86-64, 4,096-byte pages,
GCC 16.2.1 (20260810), Clang 23.1.4. Release uses -O3 -DNDEBUG and C11.
CLOCK_MONOTONIC measures wall time. CPU frequency remains uncontrolled; there
is no privileged frequency/governor change. perf and cppcheck are unavailable.
The initial baseline/after runs below had unrestricted affinity and no overlapping
local build/test jobs. Subsequent immutable-revision comparisons use CPU 0 and
alternate revision order; the reproducible script and result summaries accompany
this document.

The v0.4 baseline was rebuilt from main f869b8435f4ac493d0fdf876712c2d0671f317a1
before development. All seven original tests passed. Both original benchmark
modes and both pressure modes ran with checked results. v0.6 is
10357084af70b3bd58753149cd7b3c61482c5c12. The first v0.7 comparison compiled the
same new benchmark source against unchanged v0.6 and updated libraries.

## Evidence and changes

An instrumented static-library probe warmed each policy, then counted 10,000
public calls. Counts, not timings, identify the overhead independently of timer
noise:

| Path | Clock reads before / after | Mutex acquisitions before / after | Heap allocations |
| --- | ---: | ---: | ---: |
| Accumulator INTERPRET | 11250 / 1250 | 20000 / 20000 | 0 / 0 |
| Integer INTERPRET | 11250 / 1250 | 20000 / 20000 | 0 / 0 |
| Accumulator TRACE | 11250 / 1250 | 30000 / 20000 | 0 / 0 |
| Warm FIXED (both representations) | 1250 / 1250 | 20000 / 20000 | 0 / 0 |

The extra clock read on every cache miss fed only ADAPTIVE's frequency window.
INTERPRET/FIXED/TRACE now omit that unused read. First-eight and periodic timed
execution samples remain. ADAPTIVE's clock-driven decision and failure cooldowns
are unchanged. Its initial profitability estimate can still decline compiling a
short program or make a timing-sensitive decision; the benchmarks report those
outcomes instead of forcing native execution to make auto look better.

A trace candidate can side-exit at most once per invocation because re-entry is
blocked locally. Two bounded bitsets now retain exit/short-exit feedback until
completion. The existing completion lock merges it in completion order and starts
the same eight-short-exit cooldown there. Call-long pins still protect every native
snapshot and interpreter continuation. Initial code acquisition and compilation
already touch the LRU, so a separate side-exit touch/lock was redundant. No RAX,
ZF, argument, PC or quantum transfer was removed. Recording and publication locks
remain. Active calls can retain already-acquired snapshots during a cooldown.

Integer kernels previously executed NOP padding in each 40-byte IR slot. Direct
kernel times were substantially above direct C even for six instructions. Exact
size layout removes that padding using a bounded 4,097-entry offset table. Generated
stack slots, operands and ABI remain unchanged; emission uses 32,776 bytes of
bounded C stack scratch. No register allocator or instruction-set expansion was
needed. Query and emission use the same size table; branch relocations target
exact instruction starts. Allocation arithmetic is bounded by finalized limits.

## Initial timing comparison

Five rotating-order samples, mixed input distribution, 50,000 calls. Median ns/call;
these are the initial unrestricted-affinity runs, with their variation retained
in local CSV and later compared by the immutable script.

| Integer workload | C before / after | Kernel before / after | Public INTERPRET before / after | FIXED before / after | ADAPTIVE before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Arithmetic | 2.067 / 2.079 | 11.063 / 4.737 | 74.002 / 67.059 | 44.844 / 46.490 | 62.364 / 44.549 |
| Bitwise | 2.066 / 2.100 | 12.703 / 5.277 | 100.817 / 66.076 | 48.319 / 42.758 | 50.653 / 45.336 |
| Conditional rule | 2.479 / 2.288 | 11.359 / 4.916 | 72.787 / 62.700 | 42.588 / 36.813 | 74.399 / 74.789 |
| Shift rule | 2.359 / 2.567 | 9.436 / 4.207 | 65.732 / 54.342 | 39.469 / 39.027 | 43.942 / 48.958 |
| Branch-heavy | 2.359 / 2.264 | 23.056 / 16.863 | 155.572 / 145.589 | 50.800 / 47.226 | 51.913 / 49.908 |

Arithmetic FIXED and shift ADAPTIVE regressed in this run. Unchanged C/interpreter
and accumulator kernels also vary, so do not attribute every difference to code
changes. All integer kernels improved substantially; end-to-end API costs and
automatic choices still dominate short evaluations. Lowering changes reduce
emitted bytes; page-rounded mapping usage need not fall for sub-page functions.

Original trace comparison at 20,000 calls, median ns/call:

| Workload | Whole JIT before / after | TRACE warm before / after | Trace kernel before / after |
| --- | ---: | ---: | ---: |
| Countdown | 58.114 / 56.922 | 212.136 / 183.930 | 106.098 / 106.102 |
| Predictable | 63.749 / 67.140 | 207.730 / 194.471 | 109.532 / 115.179 |
| Changing | 58.961 / 60.174 | 510.879 / 504.877 | 420.139 / 407.309 |
| Short | 2.591 / 2.590 | 62.076 / 54.109 | n/a |

Trace code generation itself is unchanged. TRACE still loses to whole-function
JIT, especially with interpreter continuation after changing branches. It remains
explicitly selected. The existing bounded ADAPTIVE whole-function policy is kept:
these measurements do not justify routing default calls into slower traces or
introducing noisy competition between tiers. No universal speedup is claimed.

## Correctness and reproducibility

The full local matrix passes: GCC/Clang Release, GCC/Clang ASan/UBSan Debug,
static/shared external installations and applications. Static builds have eleven
CTest cases; shared builds have ten because the wrapping-based overhead probe
requires static linkage. Focused integer/trace/adaptive/adaptive_failures pass
GCC TSan. Normal compilation retains warnings as errors. Commands use the same
v0.6 matrix with build-v07-* directory names; the concrete GCC invocation is:

```sh
cmake -S . -B build-v07-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-v07-gcc --parallel 2
ctest --test-dir build-v07-gcc --output-on-failure
```

The new steady-state probe regression asserts clock/lock/allocation counts for
INTERPRET/FIXED/TRACE without hardcoding ADAPTIVE timing choices. A compact-layout
regression checks exact byte length, forward relocation and both branch results.
All operation tests also check a sentinel beyond the queried emission length.
The capacity fixture grew to retain a genuinely multipage function after padding
removal; its rejection assertion is unchanged. Existing backedge, 9,600 random
integer comparisons, ABI sentinels, original byte/trace differential suites,
failed mapping/publication/release, budgets and concurrent cache tests remain.

GCC object -fanalyzer and Clang --analyze on changed runtime/emitter report no
diagnostics. v0.6's inspected interpreter/facts-loop analyzer limitations remain.
Sanitizers instrument C, not native instructions; leak detection is disabled as
in CI. No human review or proof of race freedom is claimed. Adversarial review
checked size-table/encoding agreement, forward/backward relocation, scratch bounds,
pin lifetime, deferred feedback ordering, lock ownership and cooldown saturation.
GNU objdump also decoded an actual emitted compact conditional: JNE targets the
second arm at 0x46, both paths restore the 16-byte frame and the image is exactly
104 bytes.

See [benchmarks/README.md](../benchmarks/README.md) for measured quantities and the
immutable comparison script. Cold totals include registration and compilation,
with construction and cleanup reported separately. Pressure suites check exact
budgets, peak mappings, evictions, hits/misses, metadata/program bytes and zero
mapped bytes after clear. Their total times can regress; no pressure speedup is
assumed. No telemetry, new runtime dependency, license or paid service was added.

## Pinned immutable-revision confirmation

Actual command after committing the tested implementation:

```sh
taskset -c 0 sh benchmarks/compare.sh feat/v0.6-execution-model \
  feat/v0.7-runtime-performance /tmp/recode-pinned-comparison 3
```

The script compared 10357084af70b3bd58753149cd7b3c61482c5c12 with
a0e85a6121d83c5d5966dc3e8e3b1252d2b355f2. There were no overlapping local
build/test/analysis jobs during measurement. Each of three alternating outer runs
contains five rotating-mode samples, giving 15 observations per cell. CPU 0
avoids migrations but does not isolate that CPU or control frequency/background
processes. [Committed CSV summaries](../benchmarks/results/README.md) preserve all
input distributions and cold counts, sample counts and observed min/max.

Mixed input, 50,000 calls, median ns/call:

| Workload | C before / after | Kernel before / after | Public INTERPRET before / after | FIXED before / after | ADAPTIVE before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Arithmetic | 2.064 / 1.949 | 9.452 / 4.466 | 61.152 / 49.881 | 38.773 / 35.747 | 64.510 / 63.557 |
| Bitwise | 1.950 / 1.671 | 10.558 / 4.457 | 67.953 / 54.794 | 39.340 / 34.667 | 70.896 / 71.293 |
| Conditional rule | 2.181 / 2.135 | 10.324 / 4.778 | 70.227 / 54.654 | 40.744 / 35.082 | 47.604 / 70.324 |
| Shift rule | 2.359 / 2.359 | 9.312 / 3.900 | 73.614 / 57.475 | 37.995 / 36.087 | 67.193 / 74.032 |
| Branch-heavy | 2.228 / 1.981 | 20.995 / 14.985 | 150.216 / 131.919 | 48.953 / 45.299 | 49.971 / 46.242 |

Arithmetic kernel ranges were 8.913..11.541 ns before and 3.900..5.105 after;
bitwise 10.304..11.989 and 4.456..5.179. Rule FIXED ranges were 38.354..46.864
and 34.184..42.908. ADAPTIVE rule ranges were 38.622..82.590 and 34.847..77.971;
its median regressed because more samples remained interpreted. Native cost
estimation and clock overhead still affect tiny functions. A hardware-specific
initial cost or forced promotion would not establish a portable improvement.
Unchanged direct interpretation also varies (arithmetic medians 19.883 / 18.746 ns).

Code size and median raw compilation time (query, emission, mapping and RX):

| Workload | Bytes before / after | Compile us before / after |
| --- | ---: | ---: |
| Arithmetic | 251 / 116 | 7.315 / 4.383 |
| Bitwise | 331 / 178 | 8.966 / 4.344 |
| Conditional rule | 411 / 209 | 7.003 / 6.506 |
| Shift rule | 331 / 163 | 6.286 / 4.346 |
| Branch-heavy | 2051 / 1275 | 10.254 / 8.893 |

For the conditional rule, FIXED cold total medians were 0.456 / 0.433 us at one
call and 6.867 / 6.242 us at 32 calls. ADAPTIVE totals were 0.393 / 0.422 us at
one call and 3.030 / 2.893 us at 32 calls. The after FIXED 32-call range widened
to 5.744..12.440 us. These tiny cold timings are noisy and include different tier
choices; registration-only measurements also varied without a registration-code
change (arithmetic median 203 / 546 ns). Construction and cleanup are measured
separately in raw output. No registration speedup is claimed.

Pinned original workloads, 20,000 calls, median ns/call:

| Workload | Whole JIT before / after | TRACE warm before / after | Trace kernel before / after |
| --- | ---: | ---: | ---: |
| Countdown | 45.262 / 46.800 | 170.251 / 149.418 | 89.383 / 87.010 |
| Predictable | 55.718 / 56.000 | 185.252 / 160.147 | 95.858 / 94.185 |
| Changing | 59.423 / 59.288 | 490.952 / 473.813 | 404.858 / 402.664 |
| Multiple paths | 82.233 / 79.099 | 206.362 / 184.615 | 118.361 / 120.229 |
| Short | 2.234 / 2.311 | 57.995 / 46.097 | n/a |

Countdown TRACE ranges were 166.392..198.234 / 148.612..158.673 ns. Changing TRACE
ranges overlapped widely: 469.349..518.491 / 448.088..600.887 ns. Trace/native
kernel code is unchanged; these results isolate most benefit to dispatch, while
not eliminating wall-clock variation. The pinned v0.4 rerun produced whole-JIT /
warm-TRACE medians of 46.073 / 169.978 ns (countdown), 56.021 / 193.243
(predictable) and 61.589 / 536.921 (changing). These differ from historical v0.4
numbers; they reinforce the need to rerun baselines on each environment.

A representative after countdown cold run compiled once, emitted 100 bytes,
entered 19,992 times, completed 2,546,383 native iterations and side-exited 19,992
times: 0.785% exits per native iteration. Available-entry hits/misses were
19,991 / 1. Compilation timing and recording timing are reported separately;
the first sample measured 12.854 us and 60 ns. These are observations with timing
noise, not pure isolated recorder costs. Whole-function and trace counters retain
their different meanings described in the runtime/tracing guides.

Pressure medians over three outer invocations:

| Suite | Size | Total ms before / after |
| --- | ---: | ---: |
| Integer | 1 function | 0.0167 / 0.0136 |
| Integer | 32 functions | 0.2279 / 0.2023 |
| Integer | 1,024 functions | 9.3239 / 8.5803 |
| Integer | 4,096 functions | 53.7404 / 52.5158 |
| Whole-function | 4 MiB | 5.202 / 5.296 |
| Whole-function | 16 MiB | 19.550 / 19.744 |
| Whole-function | 64 MiB | 83.924 / 80.042 |
| Trace | 1 page | 83.276 / 81.801 |
| Trace | 4 pages | 85.319 / 83.203 |
| Trace | 16 pages | 86.215 / 84.236 |

Whole-function 4/16 MiB totals regressed slightly. At 4,096 integer functions,
both revisions retain a 16-page peak (65,536 bytes), 4,080 evictions, 4,096 hits,
8,192 misses, 1,999,200 metadata bytes and 655,360 program bytes. Whole-function
pressure at 16,416 functions peaks at exactly 64 MiB, with 8,272,992 metadata and
262,656 program bytes, plus 32 evictions. Trace pressure compiles 4,096 entries
and evicts 4,095 / 4,092 / 4,080 at its three budgets; requested metadata/IR are
2,785,632 / 163,840 bytes. Clear reaches zero mapped bytes for every run. Peak
mapping counts are the page counts for these single-page kernels; no smaller-page
cache packing was introduced. Size reduction chiefly improves instruction fetch
and leaves these page-rounded pressure footprints unchanged.

## Post-commit quality review

The isolated C-support fork's install metadata still identifies feat/c-native-analysis
at 5179f6ba61dca2050813a1aa7e2f71ba7e4ce765. Scan, next, full displayed finding
inspection and rescan used the same commands recorded for v0.5. It displayed 48
open reports with eleven more hidden by the per-detector noise budget. Source
inspection found C import/link/test discovery gaps, reachable goto cleanup labels,
local helper signature collisions, file-size advisories and short deliberate
benchmark/mapping duplication. The new dispatch probe is itself an executed CTest
case; the integer benchmark was executed across immutable revisions. These are
not untested/orphaned modules. Shared small IR fixtures keep measured rules equal
to the independent application's expressions without coupling public consumers
to private benchmark support. No confirmed source defect arose from these reports;
none was hidden, marked fixed or excluded to improve a score. Rescan reproduced
the reports. C analysis remains incomplete; subjective scores were not used as an
acceptance criterion. Compiler analyzers, ABI/differential/failure/concurrency
regressions and adversarial source review supply separate evidence.
