#!/bin/sh
# Build immutable libraries, then run this same installed consumer against both.
set -eu
if [ "$#" -ne 3 ]; then echo 'usage: compare-revisions.sh before after new-output-directory' >&2; exit 1; fi
before=$1
after=$2
output=$3
if [ -e "$output" ]; then echo 'output already exists' >&2; exit 1; fi
mkdir -p "$output"
output=$(cd "$output" && pwd)
root=$(git rev-parse --show-toplevel)
for name in before after; do
    if [ "$name" = before ]; then ref=$before; else ref=$after; fi
    revision=$(git rev-parse "$ref")
    mkdir "$output/$name-source"
    git archive "$revision" | tar -x -C "$output/$name-source"
    cmake -S "$output/$name-source" -B "$output/$name-library" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$output/$name-prefix" > "$output/$name-build.log" 2>&1
    cmake --build "$output/$name-library" --parallel 2 >> "$output/$name-build.log" 2>&1
    cmake --install "$output/$name-library" >> "$output/$name-build.log" 2>&1
    cmake -S "$root/benchmarks/competitors" -B "$output/$name-comparison" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$output/$name-prefix" \
        -DCOMPARISON_LLVM="${COMPARISON_LLVM:-OFF}" -DCOMPARISON_MIR="${COMPARISON_MIR:-OFF}" \
        -DMIR_INCLUDE_DIR="${MIR_INCLUDE_DIR:-}" -DMIR_LIBRARY="${MIR_LIBRARY:-}" \
        >> "$output/$name-build.log" 2>&1
    cmake --build "$output/$name-comparison" --parallel 2 >> "$output/$name-build.log" 2>&1
    ctest --test-dir "$output/$name-comparison" --output-on-failure >> "$output/$name-build.log" 2>&1
    printf '%s\n' "$revision" > "$output/$name-revision.txt"
done
round=1
while [ "$round" -le "${BENCH_ROUNDS:-7}" ]; do
    if [ $((round % 2)) -eq 1 ]; then order='before after'; else order='after before'; fi
    for name in $order; do
        revision=$(cat "$output/$name-revision.txt")
        python3 "$root/benchmarks/competitors/collect.py" "$output/$name-comparison" "$output/$name-$round" \
            --rounds 1 --order-offset "$((round-1))" --cpu "${BENCH_CPU:-0}" --revision "$revision" \
            --mir-revision "${MIR_REVISION:-unavailable}"
        python3 "$root/benchmarks/competitors/collect.py" "$output/$name-comparison" "$output/$name-lifecycle-$round" \
            --rounds 1 --order-offset "$((round-1))" --cpu "${BENCH_CPU:-0}" --revision "$revision" \
            --mir-revision "${MIR_REVISION:-unavailable}" --lifecycle-only
        "$output/$name-comparison/lifecycle" > "$output/$name-registration-$round.csv"
        taskset -c "${BENCH_CPU:-0}" "$output/$name-library/recode_integer_bench" --pressure > "$output/$name-pressure-$round.csv"
    done
    round=$((round+1))
done
