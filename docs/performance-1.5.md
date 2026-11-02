# Recode v1.0 versus v1.5

Recode provides a measured cost advantage for small dynamically generated programs
with limited reuse: initialization/compilation and small-request process RSS are
lower than the measured LLVM ORC and MIR integrations. Its managed warm execution
remains slower than both competitors and optimized C. Batching improves throughput
relative to its scalar API, without closing that native-entry gap. Large caller
arrays can eliminate its RSS advantage over MIR. Choose it for the measured startup,
footprint and API trade-off, not for a claim of universally faster code.

## Revisions and method

- Baseline: v1.0.0, 35028f5bc2167e6662e67802099c7d6c5d4dcce2.
- Integrated library: 928f99883d65f44b13bae7423ccd9237fa496723, version 1.5.0.
- Scalar/context harness: that same 928f998 revision.
- Additional bulk harness: a008a1bb2579bf9c17bb4e7d6f6e0eea3a8aa818, against the
  unchanged 928f998 library. Runtime/build/test sources are identical between these revisions.
- GCC 16.2.1, Clang/LLVM 23.1.4, Intel N150, Linux 7.3.0-4-cachyos, glibc 2.44.
  Release harness/library flags -O3 -DNDEBUG; LLVM/MIR optimization level 2.
  MIR dc2c763f1c8bc746102ce87933d866335f9e6c06 is built separately, not vendored.

Both libraries were extracted from immutable archives, installed separately, and
consumed by the same comparison source. Three outer rounds alternate library order;
engine order also alternates. Scalar/context/bulk latency cells run on CPU 0 in fresh processes; pressure also
uses CPU 0. The retained-registration executable inherits the driver's CPU 0..3
affinity. The separate batch thread study explicitly uses CPUs 0..3. Frequency, other system load and
CPU isolation are uncontrolled. No local build/test job overlaps these collections.
Three samples per cell are insufficient for a universal ordering of close results.
All raw observations, environmental metadata, lifecycle, pressure and cold counts
are retained in [v15](../benchmarks/competitors/results/v15/),
[v15-batch](../benchmarks/competitors/results/v15-batch/) and
[v15-bulk](../benchmarks/competitors/results/v15-bulk/).

Exact scalar study command:

```sh
COMPARISON_LLVM=ON COMPARISON_MIR=ON MIR_INCLUDE_DIR=/tmp/recode-mir \
MIR_LIBRARY=/tmp/recode-mir-build/libmir_static.a \
MIR_REVISION=dc2c763f1c8bc746102ce87933d866335f9e6c06 \
  sh benchmarks/competitors/compare-revisions.sh v1.0.0 928f998 /tmp/recode-v15-study
```

The early baseline's cold phase included one separate first call plus N loop
calls. Adversarial methodology review found this mismatch with an exact N-call
experiment. The corrected final harness measures exactly N, checks managed cold
invocation counts before warmup and records cold_compile_ns/cold tier counters.
The first-execution timer is inside the cold interval; reference checking is outside.
Earlier raw data remain marked with their N+1 convention. Do not mix the two cold
schemas or mistake later warmup compilation for a one-call cold compilation.

Warm scalar timing includes input formation, dispatch and checksum accumulation.
Bulk timing preforms inputs outside the timed interval for every engine. These are
separate experiments, not directly interchangeable costs. The compiler adapters
use valid views; their bounds-failure sentinel is not Recode's status/offset protocol.
Unsupported invalid-view timing is excluded. C may simplify expressions and the
reference implementation is not a claim of the best possible handwritten C.

## Scalar execution

Median warm ns/evaluation at 100,000 calls:

| Workload | v1.0 interpret | v1.5 interpret | v1.0 FIXED | v1.5 FIXED | v1.5 C | LLVM | MIR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 85.866 | 85.825 | 59.519 | 59.755 | 7.959 | 8.501 | 8.888 |
| bitwise | 95.491 | 95.580 | 60.374 | 61.970 | 8.386 | 8.476 | 9.254 |
| conditional | 95.687 | 98.554 | 65.136 | 64.287 | 8.638 | 8.434 | 9.960 |
| branches | 219.205 | 221.387 | 76.084 | 72.220 | 8.382 | 21.409 | 18.869 |
| loop8 | 241.357 | 254.963 | 74.128 | 67.644 | 10.351 | 9.880 | 16.191 |
| records | 109.256 | 111.341 | 66.246 | 67.901 | 14.740 | 9.118 | 11.523 |

Loop FIXED improves by about 9% and branch-heavy FIXED by about 5% here. Arithmetic,
bitwise and record FIXED medians regress; loop interpretation also regresses. The
separate immutable native-kernel study found 8..17% gains, but that is not a public
scalar API speedup guarantee. Default policy thresholds and locking remain unchanged.
Native text shrinks; repeated validation/layout work can increase compilation cost.

FIXED compilation intervals and emitted bytes (100,000-call cells):

| Workload | Compile us v1.0 | Compile us v1.5 | Bytes v1.0 | Bytes v1.5 |
| --- | ---: | ---: | ---: | ---: |
| arithmetic | 14.869 | 17.741 | 116 | 81 |
| bitwise | 13.572 | 18.942 | 178 | 119 |
| conditional | 13.941 | 19.285 | 209 | 140 |
| branches | 19.135 | 24.140 | 1275 | 850 |
| loop8 | 14.507 | 18.422 | 196 | 138 |
| records | 18.193 | 18.676 | 370 | 275 |

All six final FIXED compilation medians regress. This is retained rather than
hidden by the earlier noisy arithmetic improvement. The optimization is justified
by smaller code and measured kernel/loop throughput, not lower compiler latency.
Page-isolated allocation still uses 4,096 executable bytes per tiny function.

Arithmetic comparison at the same count:

| Engine | Compile us | Peak RSS MiB | Warm ns/evaluation |
| --- | ---: | ---: | ---: |
| fixed | 17.741 | 2.039 | 59.755 |
| llvm | 4756.910 | 57.453 | 8.501 |
| mir | 153.434 | 3.262 | 8.888 |
| c | 0.000 | 2.023 | 7.959 |

RSS uses Linux VmHWM since exec, including shared libraries, compilation and runtime
memory. It is not private heap usage or Recode's executable ledger. The retained
ru_maxrss field can include a Python launcher's pre-exec floor. MIR text size and
LLVM/MIR executable allocation totals remain unmeasured (-1), not zero. Recode's
compilation interval and LLVM/MIR pipeline intervals have different documented
public boundaries, including finalization/validation allocation differences.

## Cold cost and limited reuse

v1.5 arithmetic end-to-end medians, us, including initialization, construction,
registration/compilation and exactly the displayed number of evaluations:

| Evaluations | Recode interpret | Recode FIXED | MIR | LLVM |
| --- | ---: | ---: | ---: | ---: |
| 1 | 29.580 | 34.277 | 737.377 | 6475.898 |
| 10 | 31.014 | 47.505 | 631.175 | 6038.699 |
| 100 | 37.396 | 50.188 | 603.968 | 6178.714 |
| 1000 | 111.174 | 104.939 | 626.777 | 6251.865 |
| 10000 | 920.412 | 694.517 | 717.063 | 6427.939 |
| 100000 | 8514.762 | 6094.341 | 1551.718 | 7118.457 |

FIXED starts interpreted, so the one-call row contains no cold JIT compilation;
small differences there are noise/setup costs, not JIT payback. For arithmetic,
FIXED's measured median cold total becomes lower than interpretation between the
100 and 1,000 evaluation samples. This is only a discrete noisy bracket. At 10,000
calls Recode and MIR arithmetic cold totals are close; at 100,000 MIR wins clearly.
LLVM's larger startup/compilation cost dominates this range, despite its faster
warm execution. More reuse eventually favors faster generated code. No portable
break-even count or cold-versus-warm comparison is inferred from unlike phases.

64 distinct functions with two checked evaluations and fresh context destruction
per function, integrated v1.5 medians:

| Engine | Total us | Aggregate compile us | Peak RSS MiB |
| --- | ---: | ---: | ---: |
| interpret | 182.470 | 0.000 | 1.922 |
| adaptive | 163.624 | 0.000 | 1.895 |
| fixed | 1006.075 | 546.198 | 1.930 |
| mir | 17879.136 | 4810.395 | 3.367 |
| llvm | 200280.785 | 149957.431 | 58.453 |

ADAPTIVE remains interpreted for these short lifetimes. This is a complete-context
experiment, not per-function unload in a retained LLVM/MIR engine. Global compiler
caches may survive context destruction. Direct C has no dynamic compilation adapter
and is excluded. The separate retained-runtime lifecycle checks 4,096 actual Recode
registration/removal cycles and retains construction/register/execute/remove costs.
The pressure experiment retains 4,096 functions with 4,080 evictions under 65,536
executable bytes; metadata is 1,999,200 and program storage 688,128 requested bytes
in both versions. Smaller text does not save pages in these fixtures. RSS and
application storage remain outside those requested-byte counters.

## Batching

The installed threshold-excess record rule uses identical preformed inputs and
result arrays for scalar and batch execution. Final v1.5 medians in ns/record:

| Items/request | Scalar FIXED | Batch FIXED | Batch / scalar |
| --- | ---: | ---: | ---: |
| 1 | 58.008 | 77.556 | 1.337 |
| 8 | 56.727 | 30.519 | 0.538 |
| 32 | 57.062 | 24.284 | 0.426 |
| 128 | 60.359 | 23.020 | 0.381 |
| 1024 | 60.059 | 24.177 | 0.403 |
| 4096 | 60.860 | 24.971 | 0.410 |
| 65536 | 61.637 | 27.617 | 0.448 |

At 128 records, FIXED is about 43 million records/s versus 17 million scalar/s.
One-item batches regress. At 65,536 records the cold batch is about 70.0 ns/record,
versus 65.1 scalar: first-batch interpretation can lose even when warm batches win.
ADAPTIVE's 65,536-record warm median regresses from 62.868 to 69.460 ns, often staying
interpreted. Results depend on tier history; defaults were not retuned for this host.

The wrapped warm probe counts 2 mutex acquisitions for 10,000 batched items versus
20,000 scalar acquisitions, 1,250 clock reads for INTERPRET/FIXED and zero allocations.
Clock sampling stays per item. The record native kernel is about 8 ns; validation,
iteration and sampling explain why public batches still cost more. The multithreaded
shared-handle addition study includes thread startup/join and correctness checking;
all 1/2/4-thread observations and tier counts are retained, with variable four-thread
scaling. No synchronization or executable ownership was removed to obtain a result.

For a direct competitor bulk comparison, a separate driver uses the exact same six
workload specifications for Recode batches and C/LLVM/MIR native loops. All preformed
inputs and output writes match. Warmup is at least eight requests/128 records and
timing covers at least eight requests/about 32,768 records. Input/backend descriptor
preparation is outside warm timing and separately reported, inside end-to-end timing.
The installed threshold-excess fixture above differs from the common compiler record
fixture; compare cross-engine values only within this common bulk experiment.

At 128 records/request, median warm ns/record:

| Workload | Recode INTERPRET | Recode FIXED | Recode ADAPTIVE | C | LLVM | MIR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 43.111 | 19.383 | 20.112 | 3.700 | 4.391 | 4.417 |
| bitwise | 54.205 | 18.105 | 19.828 | 4.599 | 3.597 | 4.417 |
| conditional | 74.647 | 19.932 | 19.999 | 6.858 | 3.720 | 5.369 |
| branches | 183.127 | 30.902 | 36.499 | 4.999 | 15.603 | 14.022 |
| loop8 | 226.875 | 31.115 | 31.202 | 5.555 | 4.920 | 11.024 |
| records | 77.185 | 23.104 | 23.033 | 5.547 | 8.538 | 7.096 |

All competitor native bulk paths still outperform Recode FIXED on these fixtures.
At 65,536 record items, Recode FIXED peak RSS is about 6.9 MiB versus MIR 5.8 MiB:
Recode's caller-side item/view descriptors can outweigh its smaller compiler/runtime
footprint. This is a real memory trade-off, not executable-budget growth. Memory-free
bulk fixtures omit unused views. LLVM still has substantially higher process RSS.

Reproduce bulk measurements by archiving a008a1b's comparison consumer, building it
against the unchanged 928f998 installed library, and running:

```sh
python3 benchmarks/competitors/collect.py /tmp/recode-bulk-final-clean /tmp/bulk-results \
  --bulk-only --cpu 0 --rounds 3 --revision 928f99883d65f44b13bae7423ccd9237fa496723 \
  --harness-revision a008a1bb2579bf9c17bb4e7d6f6e0eea3a8aa818 \
  --mir-revision dc2c763f1c8bc746102ce87933d866335f9e6c06
```

The practical niche demonstrated here is frequent creation of small integer rules,
limited reuse, a low compiler dependency footprint, and repeated records batched
through an explicitly chosen suitable tier. Long-lived hot functions favor MIR/LLVM
or C on this evidence. Next work should investigate measured batch preflight/model
costs and compilation planning overhead with the same checked fixtures.
