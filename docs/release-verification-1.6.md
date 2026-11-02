# v1.6.0 candidate verification

The implementation consists of the five stacked milestones described in
[performance-1.6.md](performance-1.6.md). The release-preparation branch sets
1.6.0 metadata; no merge, tag or release publication is authorized.
The baseline v1.5.0 tag resolves to 9f49d55296614cadeaadd7338b60bfac642a53fb.
It was created during this assignment and matches the original measured baseline.

The implementation milestones passed GCC Release and GCC ASan/UBSan regression
checks, including independent native results, ABI sentinels, guard pages,
allocation/system-call failures, blocked batch ownership and concurrency pressure.
Each milestone ran post-commit C-support-fork scan/next. Numeric quality scores
are not correctness evidence; C parsing/import/coverage limitations remain.

Final clean-archive verification must identify the exact candidate commit,
compiler matrix, installed old-source consumers, exports/SONAME, fuzz smoke,
immutable performance comparison and live GitHub CI before release readiness.
The release checklist records these acceptance gates. The existing reusable
command is sh scripts/verify-release.sh /absolute/new-results-directory from
an extracted Git-free source archive. Generated code requires independent native
execution checks in addition to C instrumentation.
