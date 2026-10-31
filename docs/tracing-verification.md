# v0.4 tracing verification

Measured on an Intel N150, four cores, Linux 7.3.0-4-cachyos x86-64, GCC
16.2.1 (20260810) and Clang 23.1.4, with 4096-byte pages. Release uses
`-O3 -DNDEBUG`, C11 and the repository warnings as errors. CLOCK_MONOTONIC
wall time includes scheduling effects. CPU affinity and frequency were not fixed.
No local build/test jobs overlapped the final trace benchmark run. These are
synthetic accumulator workloads, not application performance measurements.

## Correctness and tooling

All seven CTest cases passed in GCC Release, Clang Release, GCC ASan/UBSan Debug
and Clang ASan/UBSan Debug. The focused `trace`, `adaptive` and
`adaptive_failures` cases also passed GCC ThreadSanitizer Debug. The four compiler
configurations match `.github/workflows/ci.yml`; remote CI is checked on the PR.

```sh
cmake -S . -B build-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-gcc --parallel 2
ctest --test-dir build-gcc --output-on-failure
# Repeat with build-clang and -DCMAKE_C_COMPILER=clang.
cmake -S . -B build-sanitize-gcc -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc -DRECODE_SANITIZE=ON
cmake --build build-sanitize-gcc --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize-gcc --output-on-failure
# Repeat with build-sanitize-clang and -DCMAKE_C_COMPILER=clang.
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc -DRECODE_THREAD_SANITIZE=ON
cmake --build build-tsan --parallel 2
ctest --test-dir build-tsan -R '^(trace|adaptive|adaptive_failures)$' --output-on-failure
```

ASan leak detection was disabled as in CI; this is not a passed LeakSanitizer run.
Allocation wrappers separately check retained heap allocations and mappings.
Generated instructions are not sanitizer-instrumented. ThreadSanitizer covers C
recording/publication/accounting and API concurrency; code lifetime also requires
manual ownership inspection and deterministic pin barriers.

GCC `-fanalyzer` with repository warning flags and Clang `--analyze` ran over all
eight library translation units without diagnostics. A broader clang-tidy run
used `-checks=-*,clang-analyzer-*,bugprone-*,performance-*`. Its 14 displayed
advisories were inspected: Linux/POSIX feature macros, safe constant widening,
bool comparison accumulation, unchanged adjacent integer API parameters,
size-bounded memcpy/memset and the asserted Linux function-pointer representation.
An opt-in zero-byte allocation report assumes successful decoding can return an
empty program; rc_decode/rc_validate reject that case. These were not confirmed
runtime defects. The first clang-tidy invocation had no enabled checks; it was
rerun explicitly with the checks above. cppcheck, perf and actionlint are absent.

Objdump independently decoded a generated countdown trace, including ENDBR64,
argument/state loads, arithmetic, forward guard relocation, snapshots, backward
rel32 and flag reconstruction. The quantum uses SETNZ DL, DEC RCX and TEST DL,DL:
TEST restores the original ZF on both the loop and yield edges. Instruction
semantics were checked against the [Intel instruction reference](https://cdrdv2-public.intel.com/789581/325383-sdm-vol-2abcd.pdf).

## Benchmark method

Run `recode_trace_bench` and `recode_trace_bench --pressure` as documented in
[tracing.md](tracing.md). The default driver checks all timed checksums and rotates
mode order for five rounds at 1, 32 and 20000 calls. Inputs cycle through 1..256.
Warm TRACE includes eight checked calls with argument 256 before steady timing;
its total_us still includes those calls, setup, recording and compilation.
Whole JIT is the existing optimized v0.3 emitter, whose source is unchanged.
The fixed runtime adds handle lookup, pinning and profiling around whole JIT.

The trace-kernel mode uses an internal native entry beginning after INPUT,
then completes any side exit with the same interpreter continuation used by the
runtime. It includes state setup and exit handling, and omits runtime lookup,
profiling and ownership. All native modes return the same results. Direct
optimized C can remove these loops entirely, so it executes different CPU work.
Recording-only includes observer discovery and then unobserved interpreter
continuation. Its C compiler loop layout can be faster than the standalone
interpreter; subtracting those times is not a pure measurement of recording cost.
Recording path time is reported independently, with timer and lock-wait noise.

Five-round median ns/call at 20000 calls:

| Workload | Direct C | Interpret | Whole JIT | Fixed runtime | Record only | TRACE cold | TRACE warm | Trace kernel |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| countdown | 2.091 | 659.264 | 57.872 | 93.281 | 414.747 | 214.162 | 216.375 | 109.984 |
| predictable | 1.386 | 1531.506 | 68.876 | 101.920 | 1419.392 | 218.194 | 216.999 | 114.242 |
| changing | 1.456 | 1265.288 | 69.173 | 107.183 | 867.271 | 580.913 | 554.714 | 466.366 |
| paths | 2.119 | 1594.502 | 99.658 | 133.634 | 1229.903 | 262.545 | 260.798 | 154.076 |
| short | 1.386 | 4.928 | 2.590 | 34.757 | 66.698 | 66.736 | 67.039 | n/a |

TRACE improves on standalone interpretation for these loop workloads, but is
slower than optimized whole-function JIT in every measured workload. No measured
case establishes a speedup over whole JIT or direct C. The branch changing at
RAX=128 causes an early guard exit and interprets the remainder, increasing cost.
The short function has no loop, so TRACE never compiles and its runtime overhead
remains significant. No profitability guarantee is made.

Cold end-to-end median total microseconds, including setup and compilation:

| Workload | Calls | Direct C | Whole JIT | Fixed runtime | Record only | TRACE cold |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| countdown | 1 | 0.062 | 5.395 | 0.841 | 0.727 | 0.471 |
| countdown | 32 | 0.101 | 4.403 | 8.838 | 5.734 | 9.375 |
| predictable | 1 | 0.065 | 5.755 | 0.984 | 0.760 | 0.589 |
| predictable | 32 | 0.116 | 5.534 | 13.737 | 11.797 | 12.875 |
| changing | 1 | 0.057 | 5.890 | 1.106 | 0.849 | 0.656 |
| changing | 32 | 0.112 | 6.149 | 13.573 | 9.685 | 12.831 |
| paths | 1 | 0.066 | 6.164 | 1.188 | 0.927 | 0.696 |
| paths | 32 | 0.107 | 6.579 | 14.533 | 10.893 | 13.937 |
| short | 1 | 0.054 | 5.147 | 0.815 | 0.477 | 0.357 |
| short | 32 | 0.086 | 4.978 | 7.448 | 3.274 | 2.924 |

Trace compilation and observed recording costs in the 20000-call cold runs:

| Workload | Compile median us | Recording median ns | Trace bytes | Whole JIT bytes | Entries | Side exits | Complete native iterations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| countdown | 18.310 | 197 | 100 | 20 | 19992 | 19992 | 2546383 |
| predictable | 20.755 | 158 | 138 | 32 | 19992 | 19992 | 2546383 |
| changing | 23.226 | 244 | 138 | 45 | 19992 | 19992 | 1268509 |
| paths | 19.938 | 282 | 173 | 51 | 19989 | 19989 | 2467647 |

Each cold loop run compiled once and retained one page. Available-entry hits were
19991 for countdown/predictable/changing and 19988 for paths, with one eligible
recording/compilation miss. Each entered cyclic trace eventually side-exited;
side exits per completed iteration, rather than per entry, describe guard
frequency usefully here. Counter totals in this table are the first round; the
five rounds agree on path counts. Early calls have not yet
heated the candidate. Guard exits per completed native iteration were 0.785% for
countdown/predictable, 1.576% for changing and 0.810% for paths. Warm totals include the eight warmup calls.

Exit probes time state initialization, one native iteration and checked state/
exit reason, not just the branch or snapshot instruction. Five-round medians:
guard 6.012 ns (5.279..7.339). yield 5.826 ns (5.206..6.933). Both use a 100-byte trace.
An earlier JRCXZ quantum implementation measured about 1146 ns for the countdown
kernel; explicit flag save/restore reduced that to about 110 ns in a separate
run. This is a local regression fix, not a claim that tracing beats whole JIT.

## Cache pressure and lookup scaling

Trace pressure registers 4096 distinct loops and calls each three times. Five
separate invocations of `recode_trace_bench --pressure` were run. Median total ms:

| Code pages | Total ms | Compilations | Evictions | Peak executable bytes | Metadata bytes | IR bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 101.141 | 4096 | 4095 | 4096 | 2720096 | 163840 |
| 4 | 101.987 | 4096 | 4092 | 16384 | 2720096 | 163840 |
| 16 | 103.450 | 4096 | 4080 | 65536 | 2720096 | 163840 |

Every pressure run checked the budget bound and zero mapped bytes after clear.
Metadata and IR are requested persistent bytes, not RSS, and have no global quota.
Compact completed recordings avoid retaining a full 256-operation buffer for a
two-operation trace. Failure to compact retains the bounded original allocation.

For the existing whole-function pressure benchmark, a separate Release build of
main commit 06a1bec was compared with v0.4. Five rounds alternated baseline then
updated `recode_runtime_bench --pressure`. Both used the same compiler and
workloads: one mapping per distinct CONST/RET translation, plus 32 evictions.

| Budget MiB | Functions | v0.3 registration ms | v0.4 registration ms | v0.3 total ms | v0.4 total ms | v0.3 metadata | v0.4 metadata |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 1056 | 2.555 | 0.556 | 9.020 | 6.179 | 329768 | 514656 |
| 16 | 4128 | 34.608 | 1.682 | 58.358 | 25.624 | 1288232 | 2013792 |
| 64 | 16416 | 1054.198 | 9.286 | 1190.356 | 97.370 | 5122088 | 8010336 |

Growing hash buckets and constant-time LRU updates remove the full-list walks
from ordinary registration/lookup/touch. This improves the measured large-set
case at the cost of extra pointers, buckets and larger profiles. Full equality
checks remain mandatory; worst-case hash collisions and eviction scans can still
be linear. The runtime pressure benchmark and original translator benchmark were
also rerun in their ordinary modes; all checksums and cleanup assertions passed.

## Separate adversarial correctness pass

A further pass inspected entry assumptions, both guard polarities, successor
indices, conditional snapshots, MOV/INPUT flag preservation, modulo arithmetic,
optimizer boundaries, rel32 arithmetic, native register clobbers and stack use.
Every guard is checked before any later trace operation can execute. Exit SETZ
precedes scratch-register writes. PC stores zero-extend an IR index through EDX,
so targets above INT32_MAX are not sign-extended into an invalid index. Trace
length bounds keep all native displacements within rel32 range.

The ownership pass traced recording claims, alias removal, pins acquired before
unlock, publication after RX protection, failed rollback ownership, failed
invalidation disabling lookup, and exact budget debit/credit. Completed trace IR
stays immutable while local views are live. Compilation never overwrites a
retained or pinned mapping. Hash growth rebuilds both indices before freeing old
buckets; last-handle removal frees the indices. Fault sweeps, deterministic pin
barriers and concurrent TSan cases cover these paths separately from result tests.

This pass also aligned the raw kernel's interpreter continuation with runtime
fallback, removed callbacks once all candidates are blocked, shared the unlimited
interpreter continuation, and added snapshot, metadata-limit, short-exit cooldown,
registration-failure and rollback-retention regressions. No human review is
claimed. The restricted subset, one path per header, conservative call-long pins,
serialized compilation, missing global metadata quota and measured whole-JIT
regressions remain intentional limitations.

## Post-commit desloppify inspection

The post-implementation scan used the existing isolated Python environment
installed directly from `aasul/desloppify@feat/c-native-analysis`. Its installed
`direct_url.json` identifies commit
`5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`, the C grammar revision specified in
AGENTS.md. The installed `desloppify/data/global/SKILL.md` was read before using
findings. No upstream main or PyPI substitution was used.

```sh
PATH="$PWD/.venv-desloppify/jscpd/node_modules/.bin:$PATH" \
  .venv-desloppify/bin/desloppify --lang cxx \
  --exclude .venv-desloppify --exclude build-gcc --exclude build-clang \
  --exclude build-sanitize-gcc --exclude build-sanitize-clang --exclude build-tsan \
  scan --path . --no-badge
.venv-desloppify/bin/desloppify --lang cxx next
.venv-desloppify/bin/desloppify --lang cxx show '*' --status open
```

The scan completed and reported 44 open items, including two orphan reports
hidden by its display noise limit. Source inspection found no confirmed defect
requiring a source change from these reports:

| Items | Inspection |
| --- | --- |
| 12 orphaned files | All eight library translation units and four executable entry points are explicitly linked by CMake. C cross-file linkage is missing from the importer graph. |
| 10 untested modules | Seven CTest suites exercise the library and demo, including direct interpreter and trace snapshots. All three benchmark drivers were executed separately with checksums. Importer-based discovery misses these callers. |
| 2 unreachable-code reports | The decoder and CFG cleanup labels are reached by goto and exercised by failure tests. |
| 6 stale exclusions | Only the Python tool environment and generated CMake build trees are excluded. No project source is excluded. |
| 5 structural advisories | The runtime keeps one mutex/ownership model with separate functions for registration, compilation, cache and observation. The public header and three focused test drivers share their respective contracts/fixtures. File size alone did not establish a defect requiring a split. |
| 5 signature reports | One identifies the C keyword if. The others identify file-local test/benchmark helpers named require, call, create and reg; these are not shared interfaces. |
| 4 boilerplate clusters | Benchmark headers, checked statistics/cleanup and arithmetic fixtures are deliberately local. Trace/function emitters share small validation preambles but different ABIs and layouts. Combining these into a framework would obscure those contracts. |

The fork's C analysis remains incomplete. cppcheck was unavailable and security
coverage fell back to regex heuristics. Its next item incorrectly called the
interpreter untested. No finding was hidden, marked fixed or excluded to change
a score. A second scan after documentation cleanup reproduced the same findings.
Compiler analyzers, broad clang-tidy inspection, sanitizers, failure wrappers and
manual state/ownership review provide independent evidence, with the limits above.

Reported numeric results are recorded for transparency, not evidence of runtime
correctness or security. Subjective dimensions were not scored.

| Score | Value |
| --- | ---: |
| Overall | 16.9 |
| Objective | 67.4 |
| Strict | 16.9 |
| Verified | 67.4 |

| Mechanical dimension | Health | Strict |
| --- | ---: | ---: |
| Code quality | 53.3 | 53.3 |
| Duplication | 97.5 | 97.5 |
| File health | 90.7 | 90.7 |
| Security (reduced coverage) | 100.0 | 100.0 |
| Test health (incomplete C discovery) | 40.3 | 41.1 |
