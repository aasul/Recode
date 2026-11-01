# Recorded benchmark evidence

Pinned CPU 0, GCC Release, Intel N150, CLOCK_MONOTONIC. Frequency was not fixed.
v0.6 is 10357084af70b3bd58753149cd7b3c61482c5c12; v0.7 measured code is
a0e85a6121d83c5d5966dc3e8e3b1252d2b355f2. Three alternating before/after
invocations each contain five rotating-mode rounds (15 samples per cell).
`v04-accumulator.csv` has one five-round rerun from f869b8435f4ac493d0fdf876712c2d0671f317a1.

CSV summaries retain median, minimum, maximum and sample count, including every
cold count and input distribution. Pressure records retain each outer round and
the drivers' keyed metrics. `environment.txt` records OS, CPU and compiler. The
comparison script recreates the raw checksummed measurements; no build/tool state
is included here. These are observations, not portable performance guarantees.

The v1.0 preparation comparison uses main a6926c6 and the clean-archive 1.0.0
build (unchanged v0.9 runtime source). Files v07-release-baseline-*.csv and
v10-*.csv contain 15 samples/cell across three alternating invocations, including
cold total and compilation timing ranges. v10-pressure.csv retains all pressure
counters and cleanup results. See docs/release-verification.md for conditions,
regressions and limits. Earlier files retain their original summary schemas.
