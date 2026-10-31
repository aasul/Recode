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
about 2.8% slower in this run. This repeated arithmetic workload benefits from
folding; it establishes no speedup for existing applications. The benchmark does
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
