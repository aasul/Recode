# Runtime refactor verification

Baseline: `11adaa01de6b7776937589b24fbe3a05386275bb`.

`adaptive.c` keeps its private runtime state and single mutex. Translation lookup,
private construction and publication now have separate responsibilities. Slab
selection is separate from RX publication, and isolated-to-shared ownership
transfer is explicit. Scalar and batch dispatch share sampling totals and
profitability updates; the sampled batch mean retains its existing policy weight.
The shared profiling helper is inline; GCC Release emits no helper call.

`integer.c` checks instruction structure before selecting linear or control-flow
definition analysis. The linear path still allocates no facts. The control-flow
path retains its intersection analysis and external entry edge with no definitions.
`runtime.c` releases decoded IR through one cleanup path after preparation.

Public headers, exported symbols, structure layouts, instruction encodings,
optimizer decisions, thresholds and memory protection transitions are unchanged.
No behavioral correction is included. Allocation order and failed-release ownership
remain covered by the existing failure sweeps.

## Verification

Environment: Linux x86-64, AMD EPYC 9V74 virtual machine, GCC 14.2.0, Clang 19.1.7,
CMake 4.4.4. Build tools were installed outside the repository.

The original GCC Release and ASan/UBSan baselines passed 16/16 CTest tests each.
The added integer and runtime regression cases also passed against the original
source, including diagnostic precedence and batch retirement after eight slow means.

| Final configuration | CTest |
| --- | --- |
| GCC Release static | 16/16 |
| Clang Release static | 16/16 |
| GCC Release shared | 16/16 |
| Clang Release shared | 16/16 |
| GCC Debug ASan/UBSan | 16/16 |
| Clang Debug ASan/UBSan | 16/16 |
| GCC Debug ThreadSanitizer | 16/16 |

All builds use the project's warning checks with `-Werror`. Shared builds check
the public export set and SONAME. CTest includes relocated static/shared installs
and five independent example consumers. The installed comparison suite passed
53/53 tests with its default Recode/C adapters. Optional LLVM/MIR adapters were
not enabled. The Clang libFuzzer harness completed 100,000 runs with `max_len=512`.

ASan/UBSan used `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`.
Both leak-enabled attempts failed because LeakSanitizer cannot operate under the
environment's process tracing. These results do not establish leak freedom.
Sanitizers instrument C code, not emitted instructions; differential execution,
ABI sentinels, guard pages and failure injection cover native behavior separately.

## Performance

Before/after binaries use GCC Release without LTO. Checked runtime, integer,
trace, storage, batch, cold-batch, record and low-level translator drivers were
run three times, alternating revision order, pinned to CPU 0. Integer and trace
pressure modes and the dispatch operation-count assertions were also exercised.
No builds or tests ran concurrently with measurements. CPU frequency and other
host activity were not controlled.

To reduce short-sample noise, temporary copies of the integer/runtime drivers
used only the 2,000,000-call count. Integer measurements used mixed inputs only;
the five internal rounds and six execution modes were retained. Each revised
driver was compiled with `gcc -std=c11 -O3 -DNDEBUG`, matching source/generated
include directories, its corresponding static archive and `-pthread`. Three
alternating before/after processes give 15 observations per row below.
All timed checksums passed. Values are median ns/call (minimum to maximum).

| Driver/workload | Policy | Before | After |
| --- | --- | --- | --- |
| integer/arithmetic | fixed | 29.85 (28.51-35.64) | 30.01 (27.89-39.56) |
| integer/arithmetic | adaptive | 30.17 (28.50-35.56) | 30.16 (28.32-39.91) |
| integer/bitwise | fixed | 29.81 (26.23-35.46) | 29.32 (27.93-52.44) |
| integer/bitwise | adaptive | 29.88 (26.40-40.49) | 29.69 (27.84-38.96) |
| integer/rule | fixed | 30.16 (26.23-33.18) | 31.81 (28.20-41.02) |
| integer/rule | adaptive | 28.76 (26.37-36.71) | 30.59 (28.55-35.45) |
| integer/shifts | fixed | 30.03 (26.20-33.10) | 31.72 (29.28-36.66) |
| integer/shifts | adaptive | 30.12 (26.41-33.68) | 30.79 (28.36-38.47) |
| integer/branches | fixed | 34.35 (30.42-39.43) | 35.26 (33.43-39.15) |
| integer/branches | adaptive | 34.11 (30.60-41.57) | 35.21 (33.48-40.04) |
| runtime/arithmetic64 | fixed | 30.04 (27.41-33.75) | 29.72 (27.49-32.71) |
| runtime/arithmetic64 | adaptive | 30.30 (29.19-33.29) | 30.16 (27.58-35.69) |
| runtime/identity | fixed | 31.24 (27.32-35.07) | 29.03 (28.11-32.18) |
| runtime/identity | adaptive | 31.42 (28.30-50.58) | 29.91 (27.83-47.48) |
| runtime/loop32 | fixed | 43.38 (38.30-56.62) | 40.14 (37.86-57.64) |
| runtime/loop32 | adaptive | 42.49 (38.79-54.83) | 39.72 (38.09-48.76) |

Integer FIXED medians changed from -1.7% to +5.6%; adaptive medians from -0.6%
to +6.4%. Accumulator runtime medians improved by 0.5% to 7.5%. Ranges overlap,
and unchanged direct-C/native controls also varied. These shared-host measurements
are inconclusive about small regressions and do not demonstrate performance parity.
The dispatch probe still checks the existing clock, lock and allocation counts.

Session logs, raw measurements and the extended-driver reproduction script are
in `/tmp/recode-evidence` and `/tmp/recode-long-measure.py`. No benchmark defaults
or source drivers were changed in the repository.

## Post-commit quality checks

Desloppify ran and was rescanned from the specified C-support fork, pinned to
`5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`. Its C/C++ import model incorrectly
marks linked translation units and tested consumers as orphaned/untested, and
its signature matcher even treats `if` as a function. The two string-API warnings
are numeric `sscanf` conversions of a bounded `/proc/self/status` line, with no
string destination. These candidates were inspected, not treated as defects.
File-size/complexity warnings remain; splitting private runtime state solely to
satisfy those thresholds would spread the ownership and locking contract.
The optional jscpd stage failed; its retry was stopped while npm waited for access.
No subjective score or complete native C coverage is claimed.

Standalone Cppcheck 2.17.1 completed all 21 `src` translation units with no findings:

```sh
cppcheck --check-level=exhaustive --std=c11 \
  --enable=warning,performance,portability --error-exitcode=1 \
  -Iinclude -Isrc -I/tmp/recode-final-gcc-release/generated src
```

The isolated Cppcheck installation initially lacked its configuration lookup path;
that was corrected before this successful run. No source changes were needed
following the post-commit review.
