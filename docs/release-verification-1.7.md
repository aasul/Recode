# v1.7.0 candidate verification

The fully tested library/package source revision is
`a3870acfb8bff222c8c755505c0afc5b75c0c98c`. Later reporting commits contain only
documentation and measurement evidence. The immutable v1.6.0 comparison commit is
`0e4a9cca12a45dd5964ee4e9108455878b3256de`; annotated tag object
`ca2d0a2a2325efd6d39f33ee48a2334f93ec8581` peels to that commit.
No merge, v1.7 tag or release publication is authorized by this preparation.

## Tested source archive

```sh
git archive --format=tar a3870acfb8bff222c8c755505c0afc5b75c0c98c | gzip -n > /tmp/recode-v17-final-source.tar.gz
mkdir /tmp/recode-v17-final-source
tar -xzf /tmp/recode-v17-final-source.tar.gz -C /tmp/recode-v17-final-source
sh /tmp/recode-v17-final-source/scripts/verify-release.sh /tmp/recode-v17-final-verification
```

Archive SHA-256:
`cc0285356a3f0fb01cafc2ad43f90d2f68c7fb55b1dc90bfa93abe0cbf9bbc8c`.
This checksum identifies the tested implementation archive, not the later
reporting head or a future merged release archive. The PR records the later
reporting archive checksum separately. Compare its src/include/tests/cmake/examples/
scripts/fuzz trees, CMakeLists.txt and LICENSE against this tested revision.
A maintainer must independently verify the merged source before publishing.

The extracted source is Git-free. Host: Intel N150, Linux 7.3.0-4-cachyos x86-64,
glibc 2.44, GCC 16.2.1, Clang 23.1.4. The retained
[verification logs](../benchmarks/v1.7/final/verification) are the evidence for the
following completed runs; these are not newly claimed reruns during reporting.

| Configuration | Actual result | Log |
| --- | --- | --- |
| GCC Release static | 16/16 passed | gcc-release.log |
| Clang Release static | 16/16 passed | clang-release.log |
| GCC Release shared | 16/16 passed | gcc-shared.log |
| Clang Release shared | 16/16 passed | clang-shared.log |
| GCC Debug ASan/UBSan | 16/16 passed | gcc-sanitize.log |
| Clang Debug ASan/UBSan | 16/16 passed | clang-sanitize.log |
| GCC Debug TSan | 9/9 focused suites passed | gcc-tsan.log |
| GCC leak-enabled complete suite, outside ptrace | 16/16 passed | final-leak-gcc.log |
| Clang leak-enabled complete suite, outside ptrace | 16/16 passed | final-leak-clang.log |
| Clang libFuzzer smoke | 100000 runs, no reported finding | final-fuzz.log |
| Optional LLVM ORC/MIR comparison consumers | 79/79 for each revision | before/after-comparison-build.log |

The script uses ASAN_OPTIONS=detect_leaks=0 and UBSAN_OPTIONS=halt_on_error=1.
TSan runs records, slabs, batch, memory, integer, stability, trace, adaptive and
adaptive_failures. Leak-enabled runs use ASAN_OPTIONS=detect_leaks=1 outside
ptrace. The initial sandbox leak attempt failed explicitly because LeakSanitizer
cannot operate under ptrace; leak-sandbox.log retains that failure. It is not a
passing leak check. Fuzzing used the archive's copied seed corpus,
`-runs=100000 -max_len=512 -timeout=5`, and the Debug Clang fuzz build flags in
scripts/verify-release.sh's companion CI workflow. The bounded smoke is not
exhaustive fuzz coverage.

## Native semantics, machine state and ownership

The native batch entry is private. Scalar and batch bodies share integer
allocation/lowering and support the full validated IR: arithmetic, bitwise,
conditions, branches, loops and bounded loads. RDI receives the private context;
RBX/RBP/R12..R15 are saved/restored once. All exits restore RSP; the leaf has no
C calls or aligned vector spills. Executed failed loads exit before the result
store and preserve the fault instruction index. Successful results form the exact
prefix. Required timed items keep scalar entries and sample positions.

[Native ABI documentation](native-batch.md) describes stack/register use and the
relative-branch invariant. Direct assembly sentinels check callee-saved state,
scalar/fused descriptor/record entries, maximum IR, high register pressure and
backedges. Tests include every operation/alias, 19200 primary randomized cases
plus fused descriptor/record comparisons, all bounded-load widths and guard pages.
Sanitizers instrument C, not generated native instructions. Independent C results,
interpreter comparisons and direct native probes are necessary additional evidence.

[Slab design](code-slabs.md) uses immutable page replacement. A fresh RW page
receives live PIC code and new code, becomes RX, and is published under the mutex.
No published mapping becomes writable, no writable alias exists, and no RWX map
is introduced. All internal branch displacements remain relative when both bodies
are copied together; no embedded code address, external call or RIP-relative data
requires relocation. Slab tests execute relocated scalar and fused entries across
all six workloads, retain aliases, remove individual owners and replace pages
concurrently. An active pin excludes the entire source page from copying.
The request retains its mapping pin while executing outside the mutex.

Tests exercise malloc/mmap/mprotect/munmap failures, failed publication rollback,
retained retired pages, retryable clear/destroy, exact budget fallback, pinned
source exclusion and concurrent calls/invalidation. Failed unmaps retain ownership
and remain charged. Removing a member cannot free another live member's mapping.
No confirmed correctness defect remained in the completed review; this is not
formal verification or an independent human review.

## Public API and installed consumers

Version is 1.7.0, API 1, ABI major/minor 1.7, SONAME `librecode.so.1.7`.
[abi.json](../benchmarks/v1.7/final/verification/abi.json) retains the exact dynamic
symbol list and generated version header: all previous 31 exports plus exactly
`rc_runtime_call_integer_records`. Private helpers remain hidden. Existing enum
values, functions and error meanings are retained. Additive trailing config/stats
fields follow the minor-version policy. Rebuild with matching headers/library;
cross-minor binary substitution is not promised. MIT and Linux x86-64 remain.

The new `rc_integer_record_batch` describes base/count/stride/record size,
0..2 unaligned uint64 argument offsets and a bounded view. Preflight rejects
layout, span/address overflow, capacity and alias errors before evaluation.
Borrowed input ownership, ordered failure, unchanged suffixes, instruction offsets,
statistics and pins match descriptor batches. No per-record allocation or
converted descriptor array is created. See [the public contract](contiguous-records.md).
The installed contiguous-rules example uses only public headers and Recode::recode;
static/shared install tests build and run it.

The unchanged examples were extracted from v1.0.0, v1.5.0 and v1.6.0 and rebuilt
against final GCC/Clang static/shared relocated installations. All 44 builds/runs
passed: v1.0 embed/rules/data-rules (12 combinations), v1.5 and v1.6 those three
plus batch-rules (16 each). The per-consumer results are retained in
[final-source-consumers.csv](../benchmarks/v1.7/final/verification/final-source-consumers.csv).
This verifies source rebuilding and package discovery, not old binary compatibility.

## Adversarial and tool review

Review covered instruction encodings and ABI exits, register preservation, stack
state, branch range/relocation, failed-prefix stores, record arithmetic/strides,
error offsets, tier promotion/sampling, cache accounting, pins/publication races,
page lifetime, rollback/retry and measurement boundaries. Relevant runtime and
emitter invariants were checked against source and targeted tests, not inferred
from a benchmark score.

The separate .venv-desloppify environment installs the required C-support fork
`feat/c-native-analysis` at `5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`.
Post-commit objective scan/next ran for each milestone and the final tested source.
Slow duplicate detection was skipped. Only build/tool/generated trees were
excluded; project code was not excluded to improve a score. The retained fresh
archive scan/next logs are final-clean-quality.log and final-clean-quality-next.log.
Its queue recovery and stale-looking coverage counts remain suspect; no score
is treated as verification. This fork improves C grammar selection in the shared
cxx plugin, not complete native C analysis.

Inspected candidates include CMake-linked sources falsely reported orphaned or
untested, `if` parsed as a function signature, goto cleanup falsely reported
unreachable and numeric-only bounded sscanf falsely flagged for string overflow.
The large runtime file remains a maintainability observation; it does not justify
an unrelated refactor in release preparation. No confirmed behavioral defect was
found in those candidates. A docs/evidence post-commit rescan is recorded in the
PR after publication.

cppcheck is unavailable. clang-tidy is installed and was run on adaptive.c,
integer_codegen.c and integer.c with
`--checks='-*,clang-analyzer-*,bugprone-*' -- -std=c11 -Isrc -Iinclude -I<generated> -pthread`.
Earlier PR text incorrectly called clang-tidy unavailable after a chained shell
check stopped at missing cppcheck; all four PR descriptions were corrected.
Full clang-tidy output and standalone Clang analyzer logs are retained.

Candidate review:

- release_slab's unlink requires a linked slab. All callers preserve that invariant
  under the runtime mutex, including rollback and retired-list cleanup.
- Record pointer arithmetic requires non-NULL data when argument bytes are needed.
  Public preflight rejects a NULL positive span; NULL empty records permit only
  zero arity and an empty view.
- The integer facts initialization warning also occurs on unchanged v1.6 source;
  clang-tidy-baseline-integer.log retains that run. Facts are initialized and
  branch indices validated. The analyzer's loop-unrolling limit loses that state.
- memcpy_s suggestions concern bounded copies; C11 Annex K is not required.
  Pointer-representation memcpy is intentional for the supported LP64 platform.
  Register encoding casts use bounded nonnegative indices. Feature macro and
  parameter/style suggestions do not establish defects.

No warning suppression or behavior change was made merely to satisfy the tools.

## Review stack and observed CI

Observed on 2026-11-03. The four feature PRs are open and unmerged. GitHub Actions
ci completed successfully at their exact heads:

| PR | Head branch | Base | Head commit | Successful run |
| --- | --- | --- | --- | --- |
| [39](https://github.com/aasul/Recode/pull/39) | feat/v1.7-native-batch-fusion | main | da4c296fccfab70939da2bd7f24a9126707f8554 | [41524976126](https://github.com/aasul/Recode/actions/runs/41524976126) |
| [40](https://github.com/aasul/Recode/pull/40) | perf/v1.7-cold-batch-policy | feat/v1.7-native-batch-fusion | fbe5d3116c0426d3fe4dd62eaf2652473a6e74e8 | [41525955318](https://github.com/aasul/Recode/actions/runs/41525955318) |
| [41](https://github.com/aasul/Recode/pull/41) | feat/v1.7-code-slabs | perf/v1.7-cold-batch-policy | 66325a87e2ad10b37085fb7185f7fdcbd9ff7d45 | [41527561548](https://github.com/aasul/Recode/actions/runs/41527561548) |
| [42](https://github.com/aasul/Recode/pull/42) | feat/v1.7-contiguous-batch | feat/v1.7-code-slabs | a7cedcbe0366052228a58dc8301f1f918452cdcd | [41529341828](https://github.com/aasul/Recode/actions/runs/41529341828) |
| Pending publication | release/v1.7.0 | feat/v1.7-contiguous-batch | Reporting commit follows tested a3870ac | Pending |

Release branch integration commits before reporting:

- d1c2c35038c308ce9e07c49b27bc34feed1be183: metadata, docs and verification workflow.
- a2094fcecb81b1d491b677eefbe5f9c3ff97bd31: restore scalar translation-field offsets.
- a3870acfb8bff222c8c755505c0afc5b75c0c98c: native-entry/fragmentation probes.

Merge order is 39, 40, 41, 42, then release preparation. The maintainer must retarget
bases as preceding PRs merge. Tracking issue: [38](https://github.com/aasul/Recode/issues/38).
CI covers GCC/Clang release/sanitizers plus shared/installed consumers and bounded
fuzzing; it is distinct from the larger local matrix above.

## Performance and remaining limits

[performance-1.7.md](performance-1.7.md) answers all ten release questions with raw
samples, IQRs, cold/lifecycle costs, LLVM/MIR comparisons and reported regressions.
Common arithmetic at 128 improves 9.916 -> 8.535 ns/record; the separate view-profile
regresses on four workloads. Direct records reduce converted storage and typical
compact-record cost, but wide repeated traversals can regress. At 4096 constants,
committed executable storage falls about 96%, while compilation per function rises
62.2%. Scalar results remain noisy and do not establish universal preservation.
Hardware cache counters and isolated per-path record RSS were not collected.

No confirmed implementation blocker remains in this verification record. Maintainer
review of correctness and performance trade-offs, final merged-source/archive
verification, and separate release authorization remain outstanding. No tag,
release or merge was performed by this assignment.
