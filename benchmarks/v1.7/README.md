# v1.7 measurement artifacts

The baseline is immutable v1.6.0 (0e4a9cca12a45dd5964ee4e9108455878b3256de).
The final tested library source is a3870acfb8bff222c8c755505c0afc5b75c0c98c.
See docs/performance-1.7.md for methodology, phase boundaries and regressions.
Milestone directories retain earlier implementation measurements. The
integration-before-layout directory retains the first integration study and
scalar repeats that motivated restoring the scalar translation-field offsets.
The final directory contains the final immutable study and verification logs.
Do not combine absolute timings across collections with uncontrolled frequency.

Reproduce the common installed LLVM ORC/MIR/scalar study using installed optional
dependencies, before collecting any timing with other builds running:

```sh
COMPARISON_LLVM=ON COMPARISON_MIR=ON MIR_INCLUDE_DIR=/path/to/mir \
MIR_LIBRARY=/path/to/libmir_static.a MIR_REVISION=EXACT_MIR_SHA BENCH_ROUNDS=7 \
  sh benchmarks/competitors/compare-revisions.sh v1.6.0 a3870ac /tmp/recode-study
```

For each round 1..7, alternate before/after and call collect.py on that revision's
comparison directory with --bulk-only --rounds 1 --order-offset ROUND_MINUS_ONE,
--cpu 0, --revision EXACT_LIBRARY_SHA and --harness-revision EXACT_HARNESS_SHA.
Dedicated recode_batch_bench --threads diagnostics use affinity 0-3. Cold/storage
benchmarks run in three alternating revision processes; record_bench runs in
three independent candidate processes with three internal engine-order rounds.
The collectors retain every process sample, not just medians.

entry_probe.c measures direct scalar and fused native invocation counts and
execution. It bypasses the public API's preflight, policy and timed samples;
these numbers are not public API latency. Compile the same source twice with
that revision's private headers and static library. Define RECODE_HAS_FUSION only
for v1.7. For example, after the common build:

```sh
cc -O3 -DNDEBUG -std=c11 -DRECODE_HAS_FUSION \
  -I/tmp/recode-study/after-source/src -I/tmp/recode-study/after-source/include \
  -I/tmp/recode-study/after-library/generated -Ibenchmarks/competitors \
  benchmarks/v1.7/entry_probe.c benchmarks/competitors/workloads.c \
  /tmp/recode-study/after-library/librecode.a -pthread -o /tmp/entry-after
```

Use before paths and omit the definition for v1.6. Count the actual C calls into
native entries; the generated loop's per-record work makes no C call. Inputs and
C checks match between engines. The compile/publication column is one cold
setup observation per workload, not a compilation distribution. Seven alternating
process rounds provide the native-execution samples.

fragmentation.c measures full, half-removed, churn-refilled and cleared states in
one runtime. Compile with public headers and the static library, defining
RECODE_HAS_SLABS only for v1.7. The old library's unavailable occupied/free
counters are -1, not estimates. Source programs and outputs are checked in both
builds. Definitions and flags affect these diagnostic consumers, not the library.
