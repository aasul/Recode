# First translator verification

Measured on Linux x86-64 with an Intel N150, GCC 16.2.1, Clang 23.1.4, and CMake
4.4.5. Results are specific to this environment; CPU frequency and scheduling were
not controlled.

## Build and tests

These configurations compiled with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Wstrict-prototypes -Werror` and passed all three CTest tests (failures, translator,
and demo):

```sh
cmake -S . -B build-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-gcc -j4
ctest --test-dir build-gcc --output-on-failure
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build build-clang -j4
ctest --test-dir build-clang --output-on-failure
cmake -S . -B build-sanitize-gcc -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_SANITIZE=ON
cmake --build build-sanitize-gcc -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize-gcc --output-on-failure
cmake -S . -B build-sanitize-clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DRECODE_SANITIZE=ON
cmake --build build-sanitize-clang -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize-clang --output-on-failure
```

Initial sanitizer runs exposed environment/tool incompatibilities: LeakSanitizer
fails under the host's tracing, and Clang's indirect-function check reads metadata
before a generated entry point. Leak detection was disabled for these runs;
CMake disables only Clang's function-type check in sanitizer builds. ASan and the
remaining UBSan checks passed. Generated code itself is uninstrumented.

Tests include 128 combinations of arithmetic boundary immediates, all 256 imm8
values for both ADD and SUB, constant-return cases, 1,000 deterministic random
programs with 32 arguments each in both modes, and 10,000 random decoder inputs.
Reference calculations use unsigned C arithmetic with a separately implemented
sign conversion. Guard-page tests cover every
nonempty truncated prefix of the supported multibyte instruction forms. Linker
wrappers inject malloc, mmap, mprotect, and munmap failures, check requested mapping
permissions, and verify rollback and retry behavior. A live mapping is also checked
through `/proc/self/maps` for RX permissions.

## Synthetic benchmark

Command: `./build-gcc/recode_bench` from the Release configuration above. The
workload starts with MOV from the argument, executes 32 pairs of ADD 7/SUB 6, and
returns `x + 32`. The baseline uses the same runtime and emitter with optimization
disabled. Each mode compiles 1,000 times; the timed compilation includes decoding,
IR allocation, optional optimization, emission, mmap, and mprotect, but excludes
release of the finished mapping. Execution uses 10,000,000 independent calls per
round, five rounds, alternating order. The caller accumulates and verifies results;
these times include indirect-call and loop overhead.

| Measurement | Unoptimized | Optimized |
| --- | ---: | ---: |
| Emitted bytes, including entry marker | 392 | 14 |
| Mean compilation, microseconds/function | 4.303 | 4.425 |
| Median execution, nanoseconds/call | 10.532 | 2.139 |
| Execution range, nanoseconds/call | 10.043 to 11.539 | 2.041 to 2.510 |

All execution rounds returned checksum 50000315000000. Optimized compilation was
about 2.8% slower in this initial run. A second run after the rollback fix recorded
6.824/5.058 microseconds to compile and median 11.027/2.387 nanoseconds per call
(unoptimized/optimized). Execution ranges were 10.788 to 11.275 and 2.051 to 2.538
nanoseconds per call. The compilation variation between runs is substantial;
these measurements do not establish a stable compilation-time advantage. This
repeated arithmetic workload benefits from folding; it establishes no speedup for existing applications. The benchmark does
not measure cache pressure across many functions, amortization in a real workload,
or scheduling and frequency effects.

## Additional static checks

GCC `-fanalyzer -fsyntax-only` and `clang --analyze` reported no diagnostics for the
library sources. A broad clang-tidy run with `clang-analyzer-*`, `bugprone-*`, and
`cert-*` produced advisory diagnostics: signed shift counts, the required
`_GNU_SOURCE` feature macro, bounded `memcpy` calls, and implicit conversions of
addresses to `void *` in the function-pointer representation copy. Inspection
found no correctness defect in those cases. Shift counts are fixed and in range;
all shifted values are unsigned. Copy sizes are checked before emission or fixed
by a static assertion. Annex K `memcpy_s` is not a glibc interface. No advisory was
suppressed in source merely to clear tool output.

## Post-commit quality review

Installed `desloppify[full]` into `.venv-desloppify` directly from
`aasul/desloppify@feat/c-native-analysis`, resolving to
`5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`. Read the installed tool's bundled
instructions, then ran scan and next after implementation commits. Only the
virtual environment and generated build directories were excluded.

The initial scan's optional jscpd invocation failed. Downloaded the C/C++ grammars
with `tree_sitter_language_pack.download(['c', 'cpp'])` and installed jscpd locally
under `.venv-desloppify/jscpd` before the successful post-fix rescan:

```sh
PATH="$PWD/.venv-desloppify/jscpd/node_modules/.bin:$PATH" .venv-desloppify/bin/desloppify --lang cxx --exclude .venv-desloppify --exclude build-gcc --exclude build-clang --exclude build-sanitize-gcc --exclude build-sanitize-clang scan --path . --no-badge
.venv-desloppify/bin/desloppify --lang cxx next
.venv-desloppify/bin/desloppify --lang cxx show '*' --status open
```

The rescan reported seven orphaned C files, eight untested modules, and five
notices about excluded tooling/build directories. The orphaned reports miss C
linkage through header declarations and CMake targets, including both main
functions. The test reports miss the calls from the CTest executables and the
separately executed benchmark. The internal header is exercised by those library
calls. These reports were rejected after inspecting CMake targets and test calls. The excluded directories contain generated output and tool
dependencies. No project source was excluded, and no finding was marked fixed to
inflate a score. No actionable source defect was confirmed from these findings.
cppcheck was unavailable, and the security phase used reduced regex coverage.

For reproducibility, the tool reported overall/strict 13.0, objective/verified
52.2, file health/duplication/security 100, code quality 59.2, and test health 11.5.
Its 20 subjective dimensions were unassessed, automatically counted as zero.
These scores include the discovery limitations above and are not evidence of
correctness or security.

Manual resource-lifetime review reproduced a lost mapping when both mprotect
and rollback munmap fail. Commit `e3be22c` retains that non-executable mapping for
release retry and adds the combined-failure regression test. All three tests
passed again in GCC/Clang Release and ASan/UBSan builds. GCC's analyzer and Clang's
static analyzer also completed without diagnostics after the fix. The rescan
above followed that commit. No broader subjective scoring review was performed.
