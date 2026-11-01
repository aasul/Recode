# v0.8 verification

Baseline: main a6926c6, GCC Release, all 11 CTests passed before edits.
v0.8: all 12 CTests pass GCC and Clang Release and GCC and Clang Debug ASan/UBSan.
The suites build all three external examples against relocated static and shared
installs for each compiler. Focused memory/integer/trace/adaptive/adaptive_failures
also pass GCC ThreadSanitizer. Leak detection was disabled for ASan runs, as in CI.
Generated instructions are not sanitizer-instrumented. GCC -fanalyzer on the
integer emitter and Clang --analyze on the emitter/runtime reported no diagnostics.
GNU as/objdump independently confirmed the new register transfers, unsigned
comparisons, addressing modes, zero-extending loads and result stores.

Actual build configurations, substituting gcc or clang and distinct build paths:

```sh
cmake -S . -B /tmp/recode-v08 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/recode-v08 --parallel 2
ctest --test-dir /tmp/recode-v08 --output-on-failure
cmake -S . -B /tmp/recode-v08-san -DCMAKE_BUILD_TYPE=Debug -DRECODE_SANITIZE=ON
cmake --build /tmp/recode-v08-san --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/recode-v08-san --output-on-failure
cmake -S . -B /tmp/recode-v08-tsan -DCMAKE_BUILD_TYPE=Debug -DRECODE_THREAD_SANITIZE=ON
cmake --build /tmp/recode-v08-tsan --parallel 2
ctest --test-dir /tmp/recode-v08-tsan -R '^(memory|integer|trace|adaptive|adaptive_failures)$' --output-on-failure
```

Performance used the same Intel N150, Linux 7.3.0-4-cachyos, GCC 16.2.1,
Clang 23.1.4 environment as v0.7. GCC Release is -O3 -DNDEBUG, CLOCK_MONOTONIC,
CPU 0 affinity, uncontrolled frequency and background scheduling. No overlapping
local build/test jobs ran during timing. One before/after invocation, five
rotating-mode samples per distribution and call count. This is weaker evidence
than alternating repeated immutable revisions; no speedup claim is made.
Committed CSV summaries in benchmarks/results retain medians and min/max.

Mixed distribution, 50,000 calls, median ns/call:

| Path | v0.7 | v0.8 |
| --- | ---: | ---: |
| Arithmetic direct interpretation | 21.581 | 27.336 |
| Arithmetic managed FIXED | 44.139 | 40.510 |
| Conditional managed interpretation | 57.771 | 72.390 |
| Conditional managed FIXED | 43.081 | 46.910 |
| Branch-heavy direct interpretation | 91.798 | 127.574 |
| Branch-heavy managed interpretation | 130.145 | 169.682 |
| Branch-heavy managed FIXED | 47.729 | 52.790 |

The expanded interpreter dispatch and view/error plumbing add costs. The
branch-heavy interpreter regression is material and accepted for the additive
memory contract in this milestone, with no performance guarantee. Memory-free
native bytes retain the same layout and ABI; unchanged direct C/kernel times
also vary. ADAPTIVE sometimes stays interpreted and can regress substantially.
Do not infer that unchanged kernels got faster from these timings. The original
accumulator trace results are retained alongside integer results.

The new data application measures direct C, direct interpretation, managed
INTERPRET/FIXED/ADAPTIVE over 1/32/50,000 calls, five rotating rounds and three
input distributions. Build the independent application in Release before timing:

```sh
cmake -S examples/data-rules -B /tmp/recode-data-bench -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/recode-v08/embedding-OFF/relocated
cmake --build /tmp/recode-data-bench --parallel 2
taskset -c 0 /tmp/recode-data-bench/data-rules --bench
```

`ns` covers input formation, checks and checksum accumulation in the call loop.
`total_us` additionally includes runtime creation/registration/compilation but
excludes builder construction and destruction. Compilation nanoseconds and native
call counts are explicit. Direct C has equivalent results but can do less work.
The library does not outperform direct C here. The new benchmark is diagnostic,
not a speed threshold for CI. Existing pressure tests still enforce executable
budgets and zero live mappings after clear.

Measured application medians, mixed distribution, 50,000 calls (ns/call):

| Mode | Median | Min | Max |
| --- | ---: | ---: | ---: |
| api-interpret | 89.936 | 85.512 | 106.144 |
| auto | 54.659 | 42.526 | 66.772 |
| c | 9.576 | 9.140 | 13.258 |
| fixed | 45.967 | 42.944 | 50.865 |
| interpreter | 60.078 | 55.833 | 67.226 |

Post-commit quality review used the isolated Python environment whose direct-url
metadata identifies aasul/desloppify feat/c-native-analysis at 5179f6b. A scan
without output exclusions encountered old nested build trees; the corrected scan
excluded only build-* directories and the quality-tool environment. The fast scan,
next, inspection of all 50 open reports and rescan completed. Full duplicate
analysis stalled in the optional jscpd phase; its result is not claimed.
cppcheck is unavailable, so the tool's security phase uses incomplete heuristics.
The reported missing C importers/tests do not model CMake linkage or CTest's
installed examples. Cleanup labels in decoder.c/ir.c are reachable goto targets;
file-local helpers need not share signatures, and `if` is not a function.
File-size/type-count advisories did not identify a source defect. No report was
marked fixed or hidden to improve a score; no project source was excluded.
Compiler analysis, guard pages, ABI checks and differential/failure/concurrency
suites provide separate evidence. No independent human review is claimed.
