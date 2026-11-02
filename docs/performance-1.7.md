# v1.7 measurements

## Native fusion milestone

Baseline: immutable v1.6.0 / 0e4a9cca12a45dd5964ee4e9108455878b3256de.
Candidate: the native-fusion implementation in this commit. The retained manifest
labels it fusion-precommit; it was measured before committing the unchanged
implementation. Seven rounds alternate revision and engine order, pinned to CPU 0
on the Intel N150, GCC 16.2.1, Release (-O3 -DNDEBUG). Frequency and background
activity were uncontrolled. Builds and tests completed before timing.

Raw samples and environment: benchmarks/v1.7/fusion. Inputs, independent result
checks, sample policy and harness are identical for both libraries. All seven
batch sizes and six workloads are retained. Preparation is outside execution
timing for both paths. This is an execution study, not an end-to-end application
comparison. An earlier overlapping-build diagnostic collection is not used.

Medians at 128 records, in ns/record; compilation in ns/translation:

| Workload | v1.6 batch | Fused batch | v1.6 scalar | Fused scalar | v1.6 compile | Fused compile |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 15.009 | 13.273 | 53.414 | 53.331 | 5525.120 | 6462.950 |
| bitwise | 15.775 | 14.859 | 53.857 | 54.046 | 5359.410 | 6069.130 |
| conditional | 15.210 | 14.298 | 54.828 | 54.612 | 5122.300 | 6706.890 |
| branches | 25.987 | 25.035 | 64.577 | 65.508 | 7626.320 | 9332.140 |
| loop8 | 21.967 | 18.913 | 60.174 | 60.207 | 5745.850 | 6327.170 |
| records | 18.751 | 16.906 | 56.629 | 56.639 | 5920.290 | 7608.200 |

Arithmetic improves, but does not reach the aspirational 10 ns/record target.
The default timed samples and whole-request validation remain in the public API.
Compilation emits two bodies and is more expensive. Dividing added compile cost
by the warm per-item saving is only a local amortization estimate, not a cold
promotion guarantee. Scalar differences and small-batch regressions remain in
the raw samples; no universal improvement is claimed.

Thread CSVs contain three internal rounds on CPUs 0..3, including creation/join
and result checks. All baseline rounds precede candidate rounds, so these are
concurrency diagnostics with an order limitation, not a controlled scaling claim.
Executable storage still rounds each translation to pages in this milestone.

Reproduce with both libraries built before timing:

```sh
python3 benchmarks/profiling/collect.py BASELINE_PROFILE CANDIDATE_PROFILE OUTPUT \
  --before-revision 0e4a9cca12a45dd5964ee4e9108455878b3256de \
  --after-revision CANDIDATE_SHA --rounds 7 --cpu 0
```

## Cold-batch milestone

Three independent processes per revision alternate baseline/candidate order.
Each process has seven internal rounds alternating default/opt-in order on CPU 0.
The same cold_bench.c links immutable v1.6 without RECODE_HAS_COLD_BATCH and the
candidate with it. The v1.6 opt_in labels are duplicates of its default behavior.
Unnumbered CSVs retain the initial diagnostic runs; the numbered files provide
the following medians. The environment and Release flags match the fusion study.
Runtime creation/registration, execution/compilation and destruction are included.
Rule construction, input preparation, stats queries and C checks are excluded
for every path. These are short-lived runtime measurements, not warm throughput.

Total microseconds, default / opt-in:

| Workload | 128 records | 4096 records | 65536 records |
| --- | ---: | ---: | ---: |
| arithmetic | 6.025 / 6.027 | 180.365 / 70.595 | 2945.885 / 1101.059 |
| bitwise | 7.408 / 7.448 | 223.242 / 78.052 | 3522.348 / 1034.687 |
| conditional | 7.176 / 7.220 | 216.308 / 68.775 | 3495.453 / 1027.686 |
| branches | 25.049 / 24.902 | 735.925 / 117.378 | 11850.390 / 1754.998 |
| loop8 | 27.545 / 27.446 | 861.090 / 90.745 | 14139.586 / 1405.610 |
| records | 9.629 / 9.638 | 290.485 / 89.811 | 4670.812 / 1178.023 |

The model declines tiny requests here, promotes arithmetic at 4096 records,
and benefits the large requests in this sample. Counts between tested sizes
are not measured break-even points. Early bounds failures, expensive publication,
cache pressure or different hardware can reverse the benefit. A high configured
compilation estimate deliberately declines even large batches. The default first
batch remains interpreted. LLVM/MIR cold comparisons are deferred to integration.

## Executable storage milestone

Five alternating revision processes use the same public storage_bench.c harness
for immutable v1.6 and this milestone, pinned to CPU 0 after builds/tests.
Each function returns a distinct constant. Registration includes builder work;
compilation_ns sums managed compilation counters. Lookup/execution includes
16 checked scalar passes. Cleanup is runtime destruction. Raw data, RSS,
occupied/free bytes, peak bytes, mappings and failures are in benchmarks/v1.7/storage.
VmHWM is cumulative within each process across increasing counts, not an isolated
per-case RSS measurement. The pressure diagnostic runs baseline before candidate.

| Functions | v1.6 committed bytes | Slab committed bytes | v1.6 compile us/function | Slab compile us/function |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 4096 | 4096 | 15.691 | 21.107 |
| 16 | 65536 | 4096 | 6.354 | 9.067 |
| 64 | 262144 | 8192 | 6.125 | 8.946 |
| 256 | 1048576 | 32768 | 5.495 | 8.068 |
| 1024 | 4194304 | 131072 | 5.392 | 8.686 |
| 4096 | 16777216 | 524288 | 5.391 | 9.318 |

At 4096 constants, committed storage falls from 16 MiB to 512 KiB (96.875%).
This includes the larger fused translations; native text itself grows relative
to scalar-only v1.6. Replacement transiently needs one extra page. Full pages
have zero reclaimable free bytes here; removals create holes counted separately.
Cleanup and lookup samples remain in the CSVs. Slabs trade compilation work
for storage; they do not improve every workload or bound total RSS.

The initial implementation scanned every translation while copying a page and
added excessive compilation cost at 4096 functions. The retained initial/ samples
led to a member list per slab; copying and pin checks now visit just that page
while candidate selection can still scan translations. This avoids the large
repeated membership scans without a general-purpose allocation framework.
Large/pinned/full-budget translations retain isolated allocation.
