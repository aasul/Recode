#!/bin/sh
# Run from an extracted source archive; no Git metadata or network required.
set -eu
if [ "$#" -ne 1 ]; then
    echo 'usage: sh scripts/verify-release.sh /absolute/new-results-directory' >&2
    exit 1
fi
case "$1" in /*) results=$1;; *) echo 'results path must be absolute' >&2; exit 1;; esac
if [ -e "$results" ]; then
    echo 'results directory must not exist' >&2
    exit 1
fi
mkdir -p "$results"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -e "$root/.git" ]; then
    echo 'extract a clean source archive first' >&2
    exit 1
fi
uname -a > "$results/environment.txt"
cmake --version >> "$results/environment.txt"
gcc --version >> "$results/environment.txt"
clang --version >> "$results/environment.txt"
for compiler in gcc clang; do
    for mode in release sanitize shared; do
        build="$results/$compiler-$mode"
        build_type=Release
        sanitize=OFF
        shared=OFF
        if [ "$mode" = sanitize ]; then build_type=Debug; sanitize=ON; fi
        if [ "$mode" = shared ]; then shared=ON; fi
        cmake -S "$root" -B "$build" -DCMAKE_C_COMPILER="$compiler" \
            -DCMAKE_BUILD_TYPE="$build_type" -DRECODE_SANITIZE="$sanitize" \
            -DBUILD_SHARED_LIBS="$shared" > "$build.log" 2>&1
        cmake --build "$build" --parallel 2 >> "$build.log" 2>&1
        ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
            ctest --test-dir "$build" --output-on-failure >> "$build.log" 2>&1
        echo "$compiler $mode passed"
    done
done
build="$results/gcc-tsan"
cmake -S "$root" -B "$build" -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug \
    -DRECODE_THREAD_SANITIZE=ON > "$build.log" 2>&1
cmake --build "$build" --parallel 2 >> "$build.log" 2>&1
ctest --test-dir "$build" -R '^(memory|integer|stability|trace|adaptive|adaptive_failures)$' \
    --output-on-failure >> "$build.log" 2>&1
echo 'gcc thread sanitizer passed'
