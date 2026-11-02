# v1.5 comparison baseline

Methodology correction: this initial cold phase executes a first call plus N
loop calls. The displayed counts below refer to the loop, so cold totals contain
N+1 evaluations. The final release study uses exactly N and explicit cold tier
counters. Initial raw data are retained; do not infer exact one-call or crossover
cost from this earlier cold convention. Warm timing is a separate phase.

The exact v1.0.0 source is 35028f5bc2167e6662e67802099c7d6c5d4dcce2, also remote main
at the start of this assignment. All 13 GCC Release CTests passed before changes.
The independent comparison has 37 passing checks, including actual LLVM ORC and
MIR execution, all six workload checksums and registration/disposal. These tests
are evidence for these adapters and workloads, not general compiler compatibility.

See [methodology](README.md) and [raw data](results/v10/raw.csv). Measured on an
Intel N150, Linux 7.3.0-4-cachyos, GCC 16.2.1, LLVM/Clang 23.1.4, -O3 -DNDEBUG for
the harness and Recode. LLVM's IR pipeline and MIR use optimization level 2.
MIR is dc2c763f1c8bc746102ce87933d866335f9e6c06. Three alternating-order rounds,
CPU 0 for latency; frequency and system load uncontrolled. Scaling data separately
uses all four allowed CPUs. No local build/test job ran during the final collection.

Median warm ns/evaluation at 100,000 calls (all inputs and verification included
as described in the methodology):

| Workload | C | Recode interpret | Recode fixed | Recode adaptive | LLVM ORC | MIR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 7.99 | 84.34 | 60.89 | 60.58 | 8.94 | 9.03 |
| bitwise | 8.70 | 97.17 | 63.93 | 61.39 | 9.38 | 9.22 |
| conditional | 8.38 | 96.30 | 66.34 | 66.38 | 8.81 | 9.92 |
| branches | 9.21 | 224.55 | 76.65 | 80.13 | 21.41 | 18.70 |
| loop8 | 10.42 | 246.53 | 77.83 | 76.78 | 9.88 | 15.16 |
| records | 14.67 | 111.75 | 67.53 | 69.67 | 9.39 | 11.61 |

Compilation intervals and process peak RSS for arithmetic, medians at the same
call count. Intervals include different public pipeline boundaries; see README.
RSS is VmHWM since exec, not ru_maxrss's inherited pre-exec floor.

| Engine | Compile us | Peak RSS MiB | Emitted text bytes |
| --- | ---: | ---: | ---: |
| c | 0.000 | 2.004 | unmeasured |
| interpret | 0.000 | 2.020 | 0 |
| fixed | 13.984 | 2.027 | 116 |
| adaptive | 14.973 | 2.027 | 116 |
| llvm | 4955.697 | 57.504 | 9 |
| mir | 134.921 | 3.234 | unmeasured |

Recode has substantially lower measured per-function compilation latency and
process RSS than LLVM on these small functions. MIR also compiles more slowly
here, but has much lower setup/RSS than LLVM. LLVM and MIR beat Recode's managed
warm throughput; C is a much faster reference for most fixtures. There is no
general execution-speed advantage. Programs must be generated dynamically for
Recode's compilation cost advantage over an ahead-of-time C function to matter.

The 100,000-call arithmetic FIXED warm loop costs about 61 ns/evaluation versus
8.9 ns for LLVM, despite Recode's tiny native kernel. Existing integer.csv retains
the separately timed direct kernel: public lookup, two mutex acquisitions, policy
and sampled clocks dominate small evaluations. dispatch.csv counts 20,000 locks,
1,250 clocks and no allocations per 10,000 warmed scalar calls. This supports a
batch API; removing ownership locks outright would be unsound.

The native stack-slot emitter also repeatedly reloads values and uses full disp32
stack addresses for the first registers. These are concrete code-size and local
data-dependency opportunities for the optimization milestone. Page-isolated
mappings waste space for tiny functions but simplify W^X publication and lifetime;
this baseline does not justify replacing that design with shared arenas.

Break-even should use the cold end_to_end_ns samples, not subtract unlike warm
phases. Construction, initialization and tier choices matter. The discrete counts
only bracket crossover and noisy cells do not establish a universal threshold:

- arithmetic: FIXED has lower median cold total than INTERPRET at measured counts 1000, 10000, 100000.

- loop8: FIXED has lower median cold total than INTERPRET at measured counts 1000, 10000, 100000.

- records: FIXED has lower median cold total than INTERPRET at measured counts 1000, 10000, 100000.

Cache pressure retains 4,096 distinct translations under 65,536 executable bytes:
4,080 evictions, 1,999,200 requested metadata bytes and 688,128 program bytes.
Tracked executable usage is not total memory. lifecycle-*.csv separately checks
4,096 actual registration/removal cycles in a retained runtime. Competitor lifetime
management is not compared by those Recode-only experiments. Trace results remain
separate accumulator workloads: integer traces are unsupported. LLVM/MIR allocation
counters and MIR text size are unmeasured, not zero. Raw samples retain all costs,
input distributions in the legacy integer driver, variability and unfavorable cases.
