# Integer profiling

See [performance-1.6](../../docs/performance-1.6.md) for methodology, limitations,
baseline evidence and reproduction. `compare.sh before after output` builds both
immutable static libraries before starting timing and uses the current harness
for both. Record the harness revision as well as the two library revisions when
publishing results. `BENCH_ROUNDS` defaults to seven.

`raw.csv` retains each process sample. Group by revision, workload, phase and
batch_size; report medians and ranges or quantiles rather than the fastest sample.
`instructions.csv` is static disassembly evidence, not a cycle estimate.
