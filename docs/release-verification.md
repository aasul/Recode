# v1.0 release verification

This is release preparation, not an official release. The implementation descends
from the three stacked milestone branches. No PR is merged and no tag or GitHub
release is created by this work. See release-checklist.md for maintainer actions.

## Reproduce from a clean archive

Create a source archive of the exact reviewed commit. Git is needed only to create
this archive; the extracted source neither contains nor requires Git metadata.

```sh
git archive --format=tar --prefix=recode-1.0.0/ --output=/tmp/recode-1.0.0.tar HEAD
gzip -n -c /tmp/recode-1.0.0.tar > /tmp/recode-1.0.0.tar.gz
mkdir /tmp/recode-release-source
tar -xzf /tmp/recode-1.0.0.tar.gz -C /tmp/recode-release-source
sh /tmp/recode-release-source/recode-1.0.0/scripts/verify-release.sh /tmp/recode-release-results
sha256sum /tmp/recode-1.0.0.tar.gz
```

The verification script requires a new absolute results directory. It runs GCC
and Clang Release, ASan/UBSan and shared Release builds, each with the full CTest
suite, then GCC TSan's six focused concurrent/runtime suites. All install tests
build uninstrumented static and shared libraries, relocate prefixes, compile
three independent consumers and execute them using installed public headers.
Shared builds check exact exports and SONAME. Logs, toolchain/environment details
and build trees remain in the results directory for inspection. Sanitizer leak
detection is disabled; generated instructions are not sanitizer-instrumented.

The release archive includes library source, headers/templates, export map,
CMake package/tests, assembly fixtures, optional fuzz harness/seeds, examples,
docs, MIT license and benchmark data. Git archive includes only tracked files,
excluding ignored/untracked build directories and quality-tool state. The MIT
license also installs under share/licenses/Recode. CI builds extracted archives
on Ubuntu for each compiler/sanitizer job and shared/fuzz smoke.

## Performance method

Use the Release tree after other build/test jobs finish. Choose an allowed CPU:

```sh
taskset -c 0 /tmp/recode-release-results/gcc-release/recode_integer_bench
taskset -c 0 /tmp/recode-release-results/gcc-release/recode_trace_bench
taskset -c 0 /tmp/recode-release-results/gcc-release/recode_runtime_bench
taskset -c 0 /tmp/recode-release-results/gcc-release/recode_integer_bench --pressure
taskset -c 0 /tmp/recode-release-results/gcc-release/recode_trace_bench --pressure
taskset -c 0 /tmp/recode-release-results/gcc-release/recode_runtime_bench --pressure
```

For the external data benchmark, configure examples/data-rules in Release against
an installed Release prefix and run `data-rules --bench`. Program construction is
separate from the timed call loop. `total_us` includes runtime initialization,
registration and compilation; cleanup/statistics queries are outside that interval.
Existing drivers retain per-compilation time, construction/registration/cleanup,
checksums and cache/accounting counters. Data results expose total initialization
and compile cost but do not separately time builder construction. TRACE has
accumulator-only recording/cold/warm/kernel modes with interpreter continuation.
Cold 1/32-call and steady 50,000-call integer/data cases must not be conflated.

Retain raw outputs and summarize with benchmarks/summarize.py. Final measurements
use Intel N150, Linux 7.3.0-4-cachyos, GCC 16.2.1 -O3 -DNDEBUG, CLOCK_MONOTONIC,
CPU 0 affinity, uncontrolled frequency/background scheduling and five rotating
samples per cell. Min/max are observation ranges, not confidence intervals.
Direct C may simplify arithmetic/loops that the IR still executes. No universal
performance gain or automatic compilation profitability is claimed. The v0.8
interpreter regression and v0.9 extra compile validation remain disclosed in
application-verification.md and stability-verification.md.

## Executed candidate checks

The Git-free validation snapshot passed all 13 CTests in each of six full builds:
GCC/Clang Release, GCC/Clang Debug ASan/UBSan, GCC/Clang shared Release. Static
builds include dispatch_overhead; shared builds substitute shared-abi. Each
configuration also runs relocated static/shared installs and all three consumers.
The six focused memory/integer/stability/trace/adaptive/adaptive_failures suites
pass GCC TSan. Optional Clang fuzzing completed 100,000 inputs from the archive.
The version header and linked library identify 1.0.0, API generation 1, ABI 1.0.
nm finds exactly 30 public functions; readelf reports SONAME librecode.so.1.0 and
only libc.so.6 as a needed library on this glibc host. Versioned CMake discovery
accepts exact 1.0.0 and rejects incompatible major 2. No abidiff or shellcheck is
available; shell syntax and source review were performed, and the verification
script itself executed the complete matrix. Local CMake is 4.4.5.

The release runtime source is unchanged from v0.9. Full-source GCC/Clang analyzer
results and inspected initialization false paths therefore remain as documented
in stability-verification.md. A Codex Security Standard scan of 383ea19 completed
with independent baseline/architecture and focused native/concurrency source
reviews, parent validation and no reportable findings. It reviewed all current
source/config/docs/seed files and parsed all benchmark data. No human review or
formal proof is claimed. Release changes add package metadata, documentation,
archive CI and a locally executed verification script; they add no runtime path.

## Final measurements

Three alternating before/after invocations, each with five rotating rounds, give
15 samples per cell. Before is main a6926c6; after is the 1.0.0 clean-archive
build with v0.9 runtime code. Existing benchmark driver sources are unchanged.
All raw outputs check results; pressure runs check mapped-byte budgets and cleanup.
Committed summaries retain call timing, total initialization/execution and compile
median/min/max. Pressure CSV retains all counters. No build/test jobs overlapped
measurement. The data consumer was independently built with Release flags.

Mixed integer inputs, 50,000 calls, median ns/call:

| Workload/path | v0.7 | v1.0 |
| --- | ---: | ---: |
| arithmetic c | 1.917 | 2.002 |
| arithmetic interpreter | 21.618 | 26.358 |
| arithmetic kernel | 4.236 | 4.841 |
| arithmetic api-interpret | 52.539 | 57.414 |
| arithmetic fixed | 37.393 | 45.815 |
| arithmetic auto | 65.377 | 50.363 |
| branches interpreter | 101.333 | 125.383 |
| branches api-interpret | 143.031 | 158.350 |
| branches fixed | 49.419 | 52.238 |

The v0.8 interpreter regression persists: the expanded dispatch and per-call
view/error plumbing add work. Arithmetic FIXED also regressed in this comparison;
its generated memory-free bytes remain unchanged, while managed dispatch changed.
Code layout, frequency and scheduling contribute to variation, so no exact causal
latency attribution is claimed. Arithmetic FIXED ranges overlap (33.576..42.886
versus 36.609..50.168 ns). ADAPTIVE medians can improve or regress depending on
measured tier decisions; they are not a portable profitability guarantee.

External data rule, mixed distribution, 50,000 calls:

| Mode | Median ns | Min | Max |
| --- | ---: | ---: | ---: |
| api-interpret | 96.276 | 87.697 | 111.385 |
| auto | 52.552 | 42.385 | 105.356 |
| c | 9.820 | 8.457 | 13.083 |
| fixed | 48.824 | 42.426 | 56.447 |
| interpreter | 65.667 | 56.743 | 79.526 |

Data FIXED steady execution improves over managed interpretation but remains
slower than direct C. At 32 cold calls its median total is 10.324 us, including
compilation, so compilation must be amortized over more calls. A rough estimate
using the 5.509 us median cold compilation and 47.452 ns steady managed saving is
about 116 additional calls; this is an inference from different measured phases,
not a guaranteed or experimentally isolated break-even threshold. Construction
and registration still need application-specific accounting.

Accumulator paths, median ns/call:

| Workload | Mode | Calls | v1.0 | Min | Max |
| --- | --- | ---: | ---: | ---: | ---: |
| loop32 | adaptive | 100000 | 48.170 | 42.062 | 54.971 |
| loop32 | direct-c | 100000 | 1.394 | 1.132 | 1.748 |
| loop32 | fixed | 100000 | 48.164 | 42.694 | 54.767 |
| loop32 | interpret | 100000 | 197.547 | 179.675 | 217.021 |
| loop32 | native-off | 100000 | 13.195 | 10.881 | 14.676 |
| loop32 | native-on | 100000 | 12.681 | 10.876 | 15.131 |
| countdown | direct-c | 20000 | 1.672 | 1.117 | 2.108 |
| countdown | fixed-runtime | 20000 | 93.325 | 79.939 | 110.791 |
| countdown | interpret | 20000 | 621.798 | 557.730 | 669.460 |
| countdown | record-only | 20000 | 374.352 | 326.034 | 419.869 |
| countdown | trace-cold | 20000 | 175.992 | 150.753 | 213.096 |
| countdown | trace-kernel | 20000 | 99.086 | 87.659 | 115.138 |
| countdown | trace-warm | 20000 | 177.198 | 150.515 | 204.366 |
| countdown | whole-jit | 20000 | 49.242 | 44.330 | 62.052 |

Whole-function JIT is substantially faster than tracing on these loop fixtures.
Direct C can simplify the result and is not doing identical CPU work. Trace kernel
timings omit public lookup/profiling but include interpreter guard continuation;
record-only and cold/warm paths have different setup costs. Full cold and steady
results for all workloads/distributions are retained in benchmarks/results/v10-*.csv.
The release adds capability/contracts, not a claim to outperform optimized C.

Representative integer pressure (4,096 distinct functions, 16 code pages):

- v0.7: peak 65536 bytes, 4080 evictions, 1999200 metadata bytes, 655360 program bytes.
- v1.0: peak 65536 bytes, 4080 evictions, 1999200 metadata bytes, 688128 program bytes.

The integer descriptor grows by eight requested bytes per distinct program for
the memory-entry flag and alignment; 4096 descriptors add 32 KiB. This is included
in program_bytes, separate from executable pages. Every pressure suite clears all
mappings; total process memory and allocator overhead remain outside these counters.

## Post-commit review and distribution

The final package implementation was committed as 1fe2dfe and the guides/benchmark
evidence as f9cc352. A fresh `git archive` of f9cc352 was extracted without Git
metadata and passed all 13 GCC Release CTests, including installed static/shared
consumers. Its gzip SHA-256 is
3a7da5c8fbb294e2f4b30391f615d3d98a0fa0d9efb068cf38aa81d311260ddb.
Subsequent changes record verification/checklist results only; runtime source,
package/build rules and consumers are unchanged.

After both commits, the pinned C-support fork at 5179f6b completed fast scan,
next, inspection of all 52 open candidates and rescan. Only build-* output and the
isolated quality environment were excluded. Reports remain C import/link/test
recognition gaps, reachable goto cleanup labels, file-local helper signature
collisions (including `if`) and file-size/type-count advisories. Installed data
rules and the optional fuzzer are executed tests despite their untested/orphaned
reports. No confirmed source defect arose, no project code was excluded, and no
score-driven suppression or cleanup was applied. Slow duplicate detection was
skipped after the earlier jscpd failures; cppcheck remains unavailable. The
compiler analyzers, differential/guard/ABI/failure/concurrency tests and completed
independent source audit are separate evidence, not proof of security.
