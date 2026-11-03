# v1.7 final performance study

The final library revision is `a3870acfb8bff222c8c755505c0afc5b75c0c98c`.
The baseline is the immutable v1.6.0 commit
`0e4a9cca12a45dd5964ee4e9108455878b3256de`. Its annotated tag object is
`ca2d0a2a2325efd6d39f33ee48a2334f93ec8581`, which explains the baseline label
in the common harness manifest. Both identify the same archived source.
Later release commits contain documentation and evidence only.

Raw samples, compiler/dependency details and logs are in
[benchmarks/v1.7/final](../benchmarks/v1.7/final/README.md).
[summary.csv](../benchmarks/v1.7/final/summary.csv) retains every batch-size and
record-stride comparison, including slower cases. Earlier milestone sections
below describe earlier revisions, not the final integration results.
The historical v1.6 table is not a same-session baseline for this study.

## Method and boundaries

Intel N150, Linux 7.3.0-4-cachyos x86-64, glibc 2.44, GCC 16.2.1;
LLVM/Clang 23.1.4. Release uses `-O3 -DNDEBUG`, C11, strict warnings, no LTO.
MIR source is `dc2c763f1c8bc746102ce87933d866335f9e6c06`;
its library SHA-256 is
`3380f2b367864a707026d3bdb3ab67bbca8ca07ce901d061043133feb2c588a6`.
Libraries and comparison consumers were independently rebuilt and checked before
timing. Seven processes per revision alternate revision/engine order for common
scalar/bulk/lifecycle and native-entry/thread/fragmentation diagnostics.
Cold/storage use three alternating revision processes; cold has seven internal
option-order rounds per process. Records use three candidate processes with
three internal engine-order rounds each. The record harness keeps declared
record_size=33 and varies the allocated stride; independent declared-size effects
were not measured. CPU 0 is used except the dedicated
thread diagnostic on CPUs 0..3. Frequency and background activity are uncontrolled.
No builds or scans ran during the final timing collection.

Tables use descriptive medians. Brackets give the 25th and 75th percentiles
computed with inclusive linear interpolation, not confidence intervals.
Repeated engine rounds in one process are not independent process samples.
Small differences within overlapping ranges do not establish a stable speedup.

Common bulk warm time excludes setup/compilation and includes public API
preflight, sampling and output writes. It supplies NULL views for memory-free
rules. The separate integer profile supplies views for every item. The record
harness uses deliberately unaligned fields and per-record views. Absolute values
must not be mixed across these boundaries. Common `end_to_end_ns` includes
engine creation, program construction/validation, registration, input allocation
and preparation, and the first bulk call; cleanup is separate. FIXED's first
batch still interprets by default; its warm-run compile counter is not part of
that first-call end-to-end duration. The dedicated cold harness includes runtime
creation/registration, compilation/execution and destruction, but consistently
excludes rule/input preparation, stats queries and C checks. Record one-request
timing includes descriptor conversion where required, but excludes heap
allocation, input generation and registration/compilation. It is not whole-process
end-to-end cost. C is a checked reference function with call/dispatch overhead,
not an inlined specialized application lower bound.

## Public batch execution and competitors

128 records, ns/record. Recode values are median [Q1, Q3] over seven processes.
LLVM/MIR/C columns use the candidate-side common-harness processes, whose
semantics and output checks match. They do not include Recode's managed policy,
statistics or timed scalar sampling.

| Workload | v1.6 | v1.7 | C | LLVM ORC | MIR |
| --- | --- | --- | --- | --- | --- |
| arithmetic | 9.916 [9.593, 10.468] | 8.535 [7.611, 8.949] | 3.115 | 2.507 | 2.812 |
| bitwise | 9.840 [9.418, 10.018] | 9.524 [9.337, 11.001] | 3.892 | 2.510 | 3.198 |
| conditional | 10.113 [9.210, 10.331] | 9.404 [8.913, 10.320] | 2.957 | 2.301 | 3.523 |
| branches | 19.776 [16.924, 19.921] | 18.591 [17.422, 20.531] | 3.507 | 11.169 | 9.867 |
| loop8 | 17.210 [14.965, 17.868] | 15.279 [13.530, 16.449] | 3.909 | 2.582 | 7.368 |
| records | 16.798 [14.793, 18.608] | 13.941 [12.498, 15.841] | 4.812 | 2.824 | 4.634 |

Arithmetic reaches the aspirational 10 ns/record goal in this common setup.
All six 128-item medians improve here, but LLVM/MIR remain faster in warm bulk
execution. That does not describe startup or a view-heavy application.

Arithmetic at each requested count, ns/record:

| Count | v1.6 | v1.7 |
| --- | --- | --- |
| 1 | 57.682 | 53.470 |
| 8 | 17.382 | 15.872 |
| 32 | 9.680 | 9.139 |
| 128 | 9.916 | 8.535 |
| 1024 | 9.395 | 7.892 |
| 4096 | 8.559 | 7.374 |
| 65536 | 8.521 | 7.435 |

Arithmetic benefits at all tested sizes in this collection. Count 1 has no
multi-record native-entry reduction, and other workloads/sizes can be slower;
see the complete summary rather than extrapolating this row to every rule.

The separate profile with views on every item gives these 128-item medians:

| Workload | v1.6 | v1.7 | Change |
| --- | --- | --- | --- |
| arithmetic | 11.455 | 9.360 | -18.3% |
| bitwise | 11.202 | 11.507 | +2.7% |
| conditional | 10.164 | 10.202 | +0.4% |
| branches | 17.896 | 19.477 | +8.8% |
| loop8 | 14.646 | 16.502 | +12.7% |
| records | 12.984 | 12.523 | -3.6% |

Bitwise, conditional, branches and loop8 regress in this public API study.
Generic descriptor/record input setup adds branches and cursor state; removing
C-to-native entries alone does not guarantee an overall application improvement.

The raw entry probe bypasses public preflight, policy and timing samples.
At 128 items over 131072 evaluations, the scalar path makes 131072 native
entries and the fused paths make 1024. Candidate arithmetic medians are
3.289 ns/record scalar, 1.955 fused descriptors and 1.836 fused records.
These are internal execution measurements, not public API latency. At default
sampling, a steady 128-record public batch typically uses eight sampled scalar
entries plus eight fused stretches. Sampling positions and counts are preserved.

## Scalar behavior and compilation

Single-thread FIXED, 100000 calls, ns/call. Main study gives median [Q1, Q3];
the focused repeat is separately retained rather than substituted for it.

| Workload | Main v1.6 | Main v1.7 | Repeat v1.6 | Repeat v1.7 |
| --- | --- | --- | --- | --- |
| arithmetic | 40.768 [39.223, 45.164] | 40.617 [38.650, 43.394] | 48.009 | 43.761 |
| bitwise | 40.858 [39.724, 43.333] | 41.720 [39.493, 46.009] | 46.205 | 41.078 |
| conditional | 45.364 [44.360, 45.896] | 44.586 [42.540, 46.292] | 52.977 | 46.033 |
| branches | 49.280 [47.804, 53.150] | 48.618 [47.060, 52.020] | 48.299 | 55.918 |
| loop8 | 46.614 [44.945, 48.252] | 42.919 [42.553, 45.586] | 46.033 | 47.852 |
| records | 46.151 [45.014, 50.476] | 46.180 [45.998, 50.575] | 50.134 | 45.460 |

Main bitwise and records medians rise; the focused repeat shows branches and
loop8 regressions. Order/frequency sensitivity remains. Restoring the original
scalar translation-field offsets restored the compiled scalar runtime_call size
(0x16d2), but does not prove universal scalar preservation. The earlier integration
study and repeats remain in integration-before-layout.

Single-thread interpreter ns/call and FIXED compilation us/translation, from the
main 100000-call cases (compilation is one observed compile per process):

| Workload | Interpret v1.6 | Interpret v1.7 | Compile v1.6 us | Compile v1.7 us | Text v1.6/v1.7 bytes |
| --- | --- | --- | --- | --- | --- |
| arithmetic | 59.014 | 61.098 | 11.802 | 24.100 | 27 / 209 |
| bitwise | 69.813 | 65.369 | 13.149 | 19.708 | 41 / 239 |
| conditional | 75.257 | 64.703 | 14.624 | 22.517 | 57 / 275 |
| branches | 162.199 | 152.622 | 16.013 | 24.922 | 430 / 1012 |
| loop8 | 186.316 | 184.731 | 12.485 | 22.808 | 51 / 265 |
| records | 79.290 | 75.874 | 16.605 | 23.093 | 248 / 663 |

The scalar-only workload still pays to emit both bodies. Arithmetic's added
12.298 us compile cost divided by its common warm saving of 1.381 ns/item is
about 8905 items. Using the view-profile saving of 2.095 ns gives about 5870.
These are local arithmetic amortization estimates across distinct measured phases,
not validated promotion thresholds. A regressing warm path has no positive
break-even based on those medians. Compiler timing and snapshot copying vary
with cache occupancy and hardware.

## Cold requests and short lifetimes

Candidate default / opt-in total microseconds, including runtime destruction:

| Workload | 128 | 1024 | 4096 | 65536 |
| --- | --- | --- | --- | --- |
| arithmetic | 5.072 / 5.093 | 37.670 / 37.696 | 146.720 / 55.350 | 2097.878 / 739.033 |
| bitwise | 5.429 / 5.442 | 40.582 / 40.611 | 164.803 / 52.115 | 2727.059 / 798.215 |
| conditional | 6.347 / 6.355 | 39.370 / 39.363 | 158.806 / 54.446 | 2576.827 / 704.602 |
| branches | 17.515 / 17.565 | 134.122 / 29.063 | 496.872 / 83.358 | 8333.057 / 1149.697 |
| loop8 | 18.687 / 18.724 | 143.490 / 144.880 | 578.862 / 68.804 | 9855.669 / 1066.060 |
| records | 6.422 / 6.462 | 52.326 / 52.316 | 214.433 / 66.161 | 3272.876 / 834.906 |

The opt-in model declines 128 items in every case here; at 1024 only branches
compiles. All tested 4096/65536 cases compile and benefit. The model estimates
static work as instruction count plus LOAD/MUL count and saving as
`4 + 3*work` ns/item. Requested saving must exceed twice the runtime compile-cost
estimate. It assumes no future reuse, cannot evict on a cold estimate, and checks
exact capacity after layout. Loops, early failures and publication cost are not
predicted. Counts between samples are not measured break-even points. High
configured compile estimates, pressure or early errors can reverse the benefit.
The default is false and default first-batch interpretation is retained.

In common arithmetic bulk at 128, first-request end-to-end medians (excluding
cleanup) are v1.7 INTERPRET 29.928 us, default FIXED 28.080 us, LLVM 4283.763 us,
and MIR 436.514 us. At 65536 they are 3398.380, 2915.221, 5057.776 and 1269.633 us.
Thus faster warm LLVM/MIR kernels have materially different startup costs.
The adapters do not supply a cold-policy-matched strided API; the cold opt-in
comparison is Recode default versus opt-in, not an LLVM/MIR policy experiment.

For 64 short-lived arithmetic functions called twice, lifecycle medians including
cleanup are INTERPRET 78.442 -> 82.105 us, FIXED 474.923 -> 588.039 us,
ADAPTIVE 85.050 -> 88.071 us, LLVM 135260.743 -> 134864.776 us, and MIR
12430.443 -> 12412.530 us. No new JIT profitability is established for tiny
short-lived programs. Interpretation/default policy avoids speculative compile
cost; opt-in cold compilation helps the measured sufficiently large single
requests. Fusion and record traversal primarily benefit already compiled work.

## Executable storage, fragmentation and pressure

Constant-return functions, three processes per revision. Managed compilation
counters include lowering/publication. Lookup is 16 checked scalar passes.
Registration includes builder work; cleanup is runtime destruction. VmHWM is
cumulative within each process across increasing counts.

| Functions | v1.6 committed | v1.7 committed | v1.7 mappings | Compile v1.6 us/function | Compile v1.7 us/function |
| --- | --- | --- | --- | --- | --- |
| 1 | 4096 | 4096 | 1 | 11.167 | 19.010 |
| 16 | 65536 | 4096 | 1 | 4.736 | 7.752 |
| 64 | 262144 | 12288 | 3 | 4.363 | 7.148 |
| 256 | 1048576 | 45056 | 11 | 4.405 | 6.814 |
| 1024 | 4194304 | 167936 | 41 | 3.603 | 6.138 |
| 4096 | 16777216 | 671744 | 164 | 4.520 | 7.329 |

At 4096 functions, committed bytes fall 16777216 -> 671744 (95.996%).
Candidate text is 610304 bytes versus 53248, aligned occupied storage 655360,
free space 16384, utilization 97.56% and peak committed 675840. Snapshot replacement
needs one spare page. Baseline uses 4096 owned mapping ranges, candidate 164;
these counts need not equal OS VMAs because adjacent mappings can coalesce.
Compilation per function rises 4.520 -> 7.329 us (62.2%). Lookup/execution totals
fall 13.057 -> 9.166 ms, cleanup 8.485 -> 1.271 ms, and cumulative VmHWM
21152 -> 5620 KiB. Storage cases report zero evictions and compilation failures.
This demonstrates executable-memory savings, not a general RSS bound.

Seven-process fragmentation diagnostic, 1024 functions:

| Phase | v1.6 committed | v1.7 committed | v1.7 occupied | v1.7 free | v1.6 phase us | v1.7 phase us |
| --- | --- | --- | --- | --- | --- | --- |
| full | 4194304 | 167936 | 163840 | 4096 | 5264.995 | 7951.217 |
| half | 2097152 | 167936 | 81920 | 86016 | 1662.067 | 68.133 |
| churn | 4194304 | 167936 | 163840 | 4096 | 2944.763 | 3863.073 |
| clear | 0 | 0 | 0 | 0 | 2480.491 | 90.524 |

Half removal leaves slab mappings committed while surviving entries own them;
free holes become usable by subsequent immutable snapshots. Churn costs more
than baseline here, while removal and final reclamation cost less. Clear returns
accounting to zero. This is one removal pattern, not arbitrary-fragmentation proof.
Large/pinned/tight-budget translations can use isolated mappings.

The retained pressure cases use a 65536-byte executable budget and checked calls.
At 4096 functions, candidate median total time/evictions and baseline values are
reported below. Failure injection and concurrent cache-pressure tests establish
correctness separately; timing is diagnostic rather than a contention model.

| Revision | Total us | Cleanup us | Evictions | Peak bytes |
| --- | --- | --- | --- | --- |
| before | 55599.649 | 379.087 | 4080.000 | 65536.000 |
| after | 58527.314 | 622.398 | 3819.000 | 65536.000 |

## Contiguous records and conversion

128 records, stride 33, ns/record. Nine engine rounds across three candidate
processes; all inputs, output checks and views represent the same logical rule.

| Workload | C | Scalar | Descriptor | Records | Descriptor request ns | Record request ns |
| --- | --- | --- | --- | --- | --- | --- |
| arithmetic | 2.156 | 34.124 | 10.805 | 4.349 | 1612.000 | 592.000 |
| bitwise | 2.449 | 35.185 | 10.773 | 4.888 | 1693.000 | 648.000 |
| conditional | 2.238 | 41.470 | 11.605 | 4.810 | 1903.000 | 644.000 |
| branches | 2.300 | 42.950 | 17.205 | 11.537 | 2434.000 | 1504.000 |
| loop8 | 4.926 | 41.780 | 16.854 | 9.922 | 2515.000 | 1305.000 |
| records | 8.882 | 36.283 | 12.171 | 6.138 | 1808.000 | 818.000 |

The arithmetic descriptor preparation median is 287 ns for 128 records.
Direct records use one 72-byte layout instead of 7168 bytes of converted
arguments/views/items (56 bytes/item). Raw records and outputs exist in both.
The CSV's zero direct-record descriptor_bytes means no per-item converted storage,
not that the layout is free. These warm runs still allocate descriptor arrays for
all modes, so their process RSS cannot establish the direct API's standalone RSS
saving. Hardware cache counters were not collected. Input-memory footprint and
stride effects are measured; cache-miss attribution is not established.

Arithmetic stride sensitivity, descriptor / records ns per item:

| Count | Stride 33 | Stride 64 | Stride 129 |
| --- | --- | --- | --- |
| 1 | 57.216 / 52.706 | 59.393 / 57.386 | 51.500 / 50.744 |
| 128 | 10.805 / 4.349 | 9.986 / 4.344 | 10.320 / 4.799 |
| 65536 | 10.289 / 4.838 | 10.478 / 6.759 | 9.636 / 12.247 |

At 65536/stride 129, direct arithmetic is slower (12.247 versus 9.636).
Avoiding conversion is not always best for repeated traversals of wide records.
The one-request measurements include conversion consistently; the warm ones
reuse prepared descriptors consistently. Neither includes program compilation.
LLVM/MIR comparisons use the common bulk harness, not a matching strided adapter.

## Concurrent execution

Dedicated FIXED diagnostic, 32768 items/thread, seven alternating revision
processes with three internal rounds. ns/item is elapsed time divided by aggregate
items and includes creation/join and checks. CPUs 0..3 are available.

| Threads | Scalar v1.6 | Scalar v1.7 | Batch v1.6 | Batch v1.7 |
| --- | --- | --- | --- | --- |
| 1 | 52.722 | 61.131 | 12.924 | 12.709 |
| 2 | 241.289 | 259.853 | 10.711 | 6.809 |
| 4 | 314.745 | 279.621 | 9.528 | 6.604 |

Shared scalar management contends: candidate one/two-thread scalar medians
regress, four threads improves. Batch medians improve here, without a universal
scaling guarantee. Native execution runs outside the runtime mutex while the
mapping pin covers the whole request. TSan and concurrent invalidation tests
remain necessary evidence beyond these throughput numbers.

## Answers to the release questions

1. Fusion reduces native-entry count and raw execution overhead. Public gains
   depend on input setup, validation and sampling; view-profile regressions remain.
2. Common arithmetic benefits at 1..65536 tested items, with larger amortization
   beyond tiny batches. Complete workload/count results include regressions.
3. Cold opt-in helps the tested 4096/65536 cases and 1024-item branches; tiny
   requests remain interpreted. It is a conservative heuristic, not a cost oracle.
4. Slabs reduce 4096-constant committed storage by about 96%, despite larger text.
5. Half removal retains partially owned pages; holes refill under churn. Snapshot
   copying increases compilation/churn cost; final cleanup is cheaper here.
6. Records avoid 56 bytes/item of converted storage and conversion work. Wide
   repeated traversals can regress; standalone RSS/cache-counter evidence is absent.
7. Both bodies and immutable snapshots add compilation work; six scalar workload
   compilation medians rise, and 4096-constant per-function cost rises 62.2%.
8. View-profile bitwise/conditional/branches/loop8, some scalar repeats, short-lived
   FIXED lifecycle, slab churn and wide-record arithmetic are slower. Raw and summary
   files retain all cases, not just these examples.
9. LLVM/MIR have lower warm bulk latency here, with much higher startup cost.
   Equivalent rule checks do not make their runtime-service boundaries identical.
10. Default interpretation remains appropriate for tiny short-lived work. Opt-in
    compilation benefits measured large single requests; warm fusion/record savings
    require enough work to cover compile and preparation costs.

No universal speedup, arbitrary-binary support or cross-minor binary compatibility
is claimed. The scope remains the public integer execution library on Linux x86-64.

---

# Earlier milestone measurements

The following sections retain contemporaneous results and limitations. Their
candidate code and collection conditions differ from the final study above.


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

## Contiguous-record milestone

The record benchmark runs three internal alternating engine rounds at each
workload/count/stride on CPU 0. Raw data is benchmarks/v1.7/records/raw-final.csv;
raw.csv retains the earlier loop diagnostic. The final loop checks zero count
once and falls through the final RETURN into the common success store.
The same complete integer lowering serves descriptor and strided input setup.

Warm medians at 128 records, stride 33, in ns/record:

| Workload | C | Scalar | Descriptor | Records |
| --- | ---: | ---: | ---: | ---: |
| arithmetic | 3.097 | 52.605 | 15.414 | 6.512 |
| bitwise | 4.282 | 53.547 | 15.651 | 7.382 |
| conditional | 3.186 | 56.870 | 16.161 | 6.645 |
| branches | 3.487 | 63.704 | 26.457 | 17.580 |
| loop8 | 6.845 | 58.562 | 22.885 | 14.428 |
| records | 13.390 | 58.415 | 18.989 | 9.712 |

Direct records meet the arithmetic target in this setup; the existing descriptor
API remains above 10 ns/record. The generic input setup adds branches and cursor
state, reducing some fusion-only gains. One-request measurements include
descriptor/argument/view preparation and are separate from repeated warm timing.
For 128 records the converted descriptor storage is 7168 bytes, versus one
72-byte record layout; raw records/results exist in both paths. Heap allocation,
input generation and registration/compilation are excluded from this warm study.
C uses the common checked reference function and includes its call/dispatch;
it is not an inlined specialized application lower bound.

The profile/ subdirectory retains a separate seven-round alternating comparison
with v1.6, using its identical input representation for both revisions. Its
results differ from the deliberately unaligned record harness; do not mix the
absolute timings. The added shared setup and text/compilation trade-offs remain
visible in those samples. This study has three internal record rounds in one
process, so cross-process uncertainty is not established for strided timings.
