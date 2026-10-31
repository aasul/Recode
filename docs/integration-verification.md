# v0.5 integration verification

Baseline: origin/main f869b84, GCC 16.2.1, Clang 23.1.4, Linux
7.3.0-4-cachyos x86-64 on Intel N150. The seven original tests passed in a fresh
GCC Release build before changes. Original runtime/trace benchmarks and both
pressure modes ran with checked results; raw output was retained locally for the
v0.7 comparison. CPU frequency and affinity were uncontrolled.

The following configuration matrix was built and tested locally. Each normal
CTest run has nine cases, including independent static/shared consumer builds
against clean installs moved to a new prefix. All nine pass with GCC Release,
Clang Release, GCC ASan/UBSan Debug, Clang ASan/UBSan Debug and GCC shared Release.
The focused trace/adaptive/adaptive_failures suites pass GCC TSan Debug.

```sh
cmake -S . -B build-v05-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-v05-gcc --parallel 2
ctest --test-dir build-v05-gcc --output-on-failure
# Repeat with Clang Release:
cmake -S . -B build-v05-clang-OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build build-v05-clang-OFF --parallel 2
ctest --test-dir build-v05-clang-OFF --output-on-failure
# Repeat for gcc and clang in separate build-v05-<compiler>-ON directories:
cmake -S . -B build-v05-gcc-ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_SANITIZE=ON
cmake --build build-v05-gcc-ON --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-v05-gcc-ON --output-on-failure
cmake -S . -B build-v05-shared -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build-v05-shared --parallel 2
ctest --test-dir build-v05-shared --output-on-failure
cmake -S . -B build-v05-tsan -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug -DRECODE_THREAD_SANITIZE=ON
cmake --build build-v05-tsan --parallel 2
ctest --test-dir build-v05-tsan -R '^(trace|adaptive|adaptive_failures)$' --output-on-failure
```

GCC `-fanalyzer` and Clang `--analyze` ran over all eight library translation
units without diagnostics, with C11 and include/src/generated include directories.
The GCC analyzer also used the repository warning flags as errors. Existing
allocation, syscall, cache and concurrency regressions remain enabled. Wrapped
failure suites link a private static test library when the installed product is
shared, because GNU linker wrapping cannot intercept internal DSO references.

Adversarial packaging review checked imported target paths, transitive Threads,
C11 propagation, private compiler options, matching version identification,
relocation, disabled tools/tests, output ownership and cleanup errors. No runtime
execution behavior changed. The public binary ABI remains experimental.
Sanitizers instrument C, not emitted machine code; leak detection is disabled as
in CI. CI results are reported on the PR, separately from local checks.
