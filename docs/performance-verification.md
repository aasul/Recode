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

See [benchmarks/README.md](../benchmarks/README.md) for measured quantities and the
immutable comparison script. Cold totals include registration and compilation,
with construction and cleanup reported separately. Pressure suites check exact
budgets, peak mappings, evictions, hits/misses, metadata/program bytes and zero
mapped bytes after clear. Their total times can regress; no pressure speedup is
assumed. No telemetry, new runtime dependency, license or paid service was added.
