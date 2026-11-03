# Final v1.7 study evidence

Library source: a3870acfb8bff222c8c755505c0afc5b75c0c98c.
Baseline: v1.6.0 commit 0e4a9cca12a45dd5964ee4e9108455878b3256de.
The ca2d0a2 baseline label in environment.json is the annotated tag object;
peeling it gives the same 0e4a9cc commit. Raw manifests are retained unchanged.

See [performance report](../../../docs/performance-1.7.md),
[verification report](../../../docs/release-verification-1.7.md) and
[reproduction notes](../README.md).

- scalar.csv and scalar-repeat.csv: main common-harness collection and a separate
  focused repeat. Select threads=1 for single-thread scalar comparisons.
- bulk.csv: common-harness public API and C/LLVM/MIR bulk measurements; NULL
  views for memory-free rules. Warm excludes preparation; end_to_end includes
  setup and first request, with cleanup separate.
- profile/: separate seven-round profile with per-item views for all rules.
- lifecycle.csv and registration.csv: short-lived and registration/removal cases.
- cold.csv: default/opt-in one-request runtime costs including destruction.
- storage.csv: many-constant compilation, lookup, mapping accounting and cumulative
  process VmHWM. The baseline occupied_bytes uses text as the unavailable-counter
  substitute; it is not a measured baseline alignment ledger.
- fragmentation.csv: full, half removed, refilled and clear phases in one runtime.
- entries.csv: private native calls, excluding public validation/policy/sampling.
- records.csv: direct C/scalar/descriptor/record execution at three strides;
  one_request_with_preparation includes conversion but not heap/program setup.
- threads.csv and pressure/: dedicated contention and cache-pressure diagnostics.
- environment.json: hardware, compilers, flags, dependency hashes and sample counts.
- summary.csv: derived median comparisons for all six workloads, seven counts and
  three record strides. Change is 100*(after/before-1); positive means slower.
  Common/profile compare revisions; records-vs-descriptor compares candidate APIs.
- verification/: actual build/test, sanitizer, fuzz, compatibility and analyzer logs.

Medians/IQRs are descriptive under uncontrolled frequency/background activity.
CSV line endings are normalized to LF and log trailing whitespace is removed;
recorded field values and diagnostic text are unchanged.
No generated binaries, build trees, Python environments or analyzer state belong
in this evidence directory. A log's score is not proof of correctness.
