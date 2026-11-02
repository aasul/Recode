#!/bin/sh
set -eu
if [ "$#" -ne 3 ]; then echo 'usage: compare.sh before after new-output-directory' >&2; exit 1; fi
before=$(git rev-parse "$1")
after=$(git rev-parse "$2")
output=$3
root=$(git rev-parse --show-toplevel)
if [ -e "$output" ]; then echo 'output already exists' >&2; exit 1; fi
mkdir -p "$output"
output=$(cd "$output" && pwd)
for label in before after; do
    if [ "$label" = before ]; then revision=$before; else revision=$after; fi
    mkdir "$output/$label-source"
    git archive "$revision" | tar -x -C "$output/$label-source"
    cmake -S "$output/$label-source" -B "$output/$label-build" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF > "$output/$label-build.log" 2>&1
    cmake --build "$output/$label-build" --parallel 2 >> "$output/$label-build.log" 2>&1
    "${CC:-cc}" -O3 -DNDEBUG -std=c11 -Wall -Wextra -Werror \
        -I"$output/$label-source/src" -I"$output/$label-source/include" -I"$output/$label-build/generated" \
        -I"$root/benchmarks/competitors" "$root/src/integer_profile.c" "$root/benchmarks/competitors/workloads.c" \
        "$output/$label-build/librecode.a" -pthread -o "$output/$label-profile"
done
python3 "$root/benchmarks/profiling/collect.py" "$output/before-profile" "$output/after-profile" \
    "$output/results" --before-revision "$before" --after-revision "$after" --rounds "${BENCH_ROUNDS:-7}"
