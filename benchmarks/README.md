# Checked performance comparisons

Build Release with GCC or Clang. Run without concurrent builds/tests:

```sh
./build/recode_integer_bench
./build/recode_integer_bench --pressure
./build/recode_runtime_bench
./build/recode_runtime_bench --pressure
./build/recode_trace_bench
./build/recode_trace_bench --pressure
./build/recode_dispatch_probe
```

Integer benchmarks rotate six modes over five rounds: direct optimized C through
a volatile function pointer, direct interpretation, internal native kernel,
managed interpretation, fixed whole-function JIT and adaptive whole-function JIT.
TRACE is covered by the accumulator driver because integer tracing is unsupported.
All results use the same inputs and checked checksums computed before timing.
The benchmark's internal kernel access is diagnostic and is not a public API.

Arithmetic, bitwise, conditional rule, shift rule and branch-heavy workloads use
mixed, predictable and equal input distributions. Counts 1, 32 and 50,000 expose
cold/hot differences. `ns` times the checked invocation loop; `total_us` includes
runtime setup, registration and compilation but excludes program construction,
statistics queries and cleanup. Those construction/registration/compilation/cleanup
costs are reported separately. Construction includes builder validation and
program destruction. Public-call timing includes API status checks. Input
formation and checksum accumulation are inside every mode's loop. Direct C can
simplify work that the IR still executes, particularly branches; compare equivalent
results rather than assuming equal CPU instruction counts.

Pressure checks 1, 32, 1,024 and 4,096 distinct integer programs under a 16-page
budget. It reports setup/execution, registration, cleanup, hits/misses, evictions,
peak bytes/pages, requested metadata and program bytes; clear must reach zero
mapped bytes. These counters exclude allocator overhead and are not RSS. Peak
pages equal peak mappings here because each pressure kernel fits one page. Whole
and trace pressure drivers cover other budgets and original representations.

The static-library dispatch probe wraps clock_gettime, pthread_mutex_lock, malloc
and calloc, warms each policy, then counts 10,000 checked calls. It is a
single-thread instrumented operation count, not an unmodified latency benchmark.
CTest asserts the documented non-adaptive steady path's counts. `--measure`
reports without those version-specific assertions for older revisions. ADAPTIVE
is measured but its compilation decisions are deliberately not hardcoded into a
clock-count assertion. No default policy, thresholds or profitability guarantee
changed in v0.7.

For immutable before/after comparisons, the script extracts both Git revisions,
builds separate static libraries, compiles the same current benchmark drivers
against each, and alternates revision order. It uses no Git worktree/branch
mutations. Refs must both support the v0.6 integer API. For example:

```sh
taskset -c 0 sh benchmarks/compare.sh feat/v0.6-execution-model \
  feat/v0.7-runtime-performance /tmp/recode-comparison 3
python3 benchmarks/summarize.py /tmp/recode-comparison/before-integer-*.csv
```

Set `CC=clang` to change compiler. CPU 0 is an example; choose a CPU allowed by
your process affinity. Frequency control is optional and should be reported, not
assumed. The output directory retains exact revisions, environment, build logs
and CSV. [Measured results](../docs/performance-verification.md) include both gains
and regressions. No performance threshold gates normal CI.
