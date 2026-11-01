#!/bin/sh
# Compare immutable source revisions with the same integer benchmark driver.
set -eu
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo 'usage: sh benchmarks/compare.sh before-ref after-ref output-directory [rounds]' >&2
    exit 1
fi
before=$1
after=$2
output=$3
rounds=${4:-3}
case "$rounds" in ''|*[!0-9]*|0) exit 1;; esac
mkdir -p "$output"
output=$(cd "$output" && pwd)
root=$(git rev-parse --show-toplevel)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
compiler=${CC:-gcc}
for name in before after; do
    if [ "$name" = before ]; then ref=$before; else ref=$after; fi
    mkdir "$temporary/$name"
    git -C "$root" archive "$ref" | tar -x -C "$temporary/$name"
    cmake -S "$temporary/$name" -B "$temporary/$name/build" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$compiler" \
        -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF > "$output/$name-build.log" 2>&1
    cmake --build "$temporary/$name/build" --parallel 2 >> "$output/$name-build.log" 2>&1
    "$compiler" -std=c11 -O3 -DNDEBUG -I"$temporary/$name/include" \
        -I"$temporary/$name/src" -I"$temporary/$name/build/generated" \
        "$root/src/integer_bench.c" "$temporary/$name/build/librecode.a" \
        -pthread -o "$temporary/$name/integer-bench"
    "$compiler" -std=c11 -O3 -DNDEBUG -I"$temporary/$name/include" \
        -I"$temporary/$name/build/generated" "$root/src/dispatch_probe.c" \
        "$temporary/$name/build/librecode.a" -pthread \
        -Wl,--wrap=clock_gettime -Wl,--wrap=pthread_mutex_lock \
        -Wl,--wrap=malloc -Wl,--wrap=calloc -o "$temporary/$name/probe"
    git -C "$root" rev-parse "$ref" > "$output/$name-revision.txt"
done
uname -a > "$output/environment.txt"
"$compiler" --version >> "$output/environment.txt"
lscpu >> "$output/environment.txt"
round=1
while [ "$round" -le "$rounds" ]; do
    # Alternate the first revision as well as each driver's internal mode order.
    if [ $((round % 2)) -eq 1 ]; then order='before after'; else order='after before'; fi
    for name in $order; do
        "$temporary/$name/integer-bench" > "$output/$name-integer-$round.csv"
        "$temporary/$name/integer-bench" --pressure > "$output/$name-integer-pressure-$round.csv"
        "$temporary/$name/build/recode_trace_bench" > "$output/$name-trace-$round.csv"
        "$temporary/$name/build/recode_trace_bench" --pressure > "$output/$name-trace-pressure-$round.csv"
        "$temporary/$name/build/recode_runtime_bench" > "$output/$name-runtime-$round.csv"
        "$temporary/$name/build/recode_runtime_bench" --pressure > "$output/$name-runtime-pressure-$round.csv"
        "$temporary/$name/probe" --measure > "$output/$name-probe-$round.csv"
    done
    round=$((round + 1))
done
