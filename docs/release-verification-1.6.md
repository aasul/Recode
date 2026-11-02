# v1.6.0 candidate verification

The tested implementation/package commit is
eeb68033b0cdaeeae53dce69e21ea59fb202ff29. Later release-branch commits add only
measurement data and documentation. The baseline v1.5.0 tag is
9f49d55296614cadeaadd7338b60bfac642a53fb. That tag/release appeared during this
assignment and matches the implementation commit used by the initial study.
No v1.6 merge, tag or release publication was performed.

## Source archive and compiler matrix

```sh
git archive --format=tar eeb6803 | gzip -n > /tmp/recode-v16-candidate.tar.gz
mkdir /tmp/recode-v16-candidate-source
tar -xzf /tmp/recode-v16-candidate.tar.gz -C /tmp/recode-v16-candidate-source
sh /tmp/recode-v16-candidate-source/scripts/verify-release.sh /tmp/recode-v16-verification
```

The deterministic gzip SHA-256 is
432e89993a7255c847e0677f40557ee48846cd4436c82544b03ff7851159a9ad.
A second independent git-archive/gzip operation matched. The archive has 291
regular files and no .git, build trees, virtual environments or analyzer state.
This checksum identifies that tested candidate, not the later documentation/data
head or a future merged release asset. Verify the final merged archive separately.

Intel N150, Linux 7.3.0-4-cachyos x86-64, glibc 2.44, GCC 16.2.1, Clang 23.1.4,
CMake 4.4.5. All runs used the Git-free archive.

| Configuration | Actual result |
| --- | --- |
| GCC Release static | 14/14 CTests passed |
| Clang Release static | 14/14 passed |
| GCC Release shared | 14/14 passed, including exact exports and SONAME |
| Clang Release shared | 14/14 passed, including exact exports and SONAME |
| GCC Debug ASan/UBSan | 14/14 passed |
| Clang Debug ASan/UBSan | 14/14 passed |
| GCC Debug TSan | 7/7 focused suites passed |
| GCC ASan/UBSan with LeakSanitizer, outside ptrace | 14/14 passed |
| Clang ASan/UBSan with LeakSanitizer, outside ptrace | 14/14 passed |

The standard script uses ASAN_OPTIONS=detect_leaks=0 and
UBSAN_OPTIONS=halt_on_error=1, matching CI. A separate leak-enabled attempt inside
the sandbox failed because LeakSanitizer cannot operate under ptrace. The same
four focused suites then passed outside that sandbox, followed by both complete
14-test sanitizer suites with ASAN_OPTIONS=detect_leaks=1. Initial failures and
successful reruns are retained under results/final/verification. They were not
misreported as passing sandbox leak checks.

Sanitizers instrument C, not generated native instructions. Native evidence
includes operation/alias tests, 19200 randomized comparisons against independent
reference results and interpretation, high-pressure backedges, original tracing
regressions, machine-state ABI sentinels, guard pages, prefix errors and malformed
private emitter inputs. GNU as independently reproduces all 27 arithmetic bytes.
This is neither formal verification nor an independent human security review.

Failure tests cover allocation sweeps, mmap/mprotect/munmap, retained rollback
mappings, capacity fallback, eviction, active mapping pins, blocked batch removal,
clear/destroy preconditions and concurrent registration/invalidation. The prepared
compiler has no fallible allocation after mmap. CFG allocation failure now proves
that no mapping was attempted; system-call sweeps retain rollback-failure coverage.

The optional Clang fuzz build used:

```sh
cmake -S /tmp/recode-v16-candidate-source -B /tmp/recode-v16-fuzz \
  -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DRECODE_BUILD_FUZZERS=ON \
  -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF
cmake --build /tmp/recode-v16-fuzz --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/recode-v16-fuzz/recode_fuzz /tmp/recode-v16-corpus -runs=100000 -max_len=512 -timeout=5
```

The corpus was copied from the archived seed corpus into the new directory before
execution. All 100000 runs completed without a reported finding. This bounded
smoke does not establish exhaustive fuzz coverage.

## Installation and source compatibility

Every compiler configuration ran independent install/relocation/consumer tests
for static and shared packages. The generated header identifies version 1.6.0,
API 1 and ABI 1.6. readelf reports librecode.so.1.6. nm's defined dynamic symbol set
matches exactly the same 31 public functions; private prepare/map helpers remain
hidden. CMake exports the relocated Recode::recode target and 1.6.0 package version.
Public recode.h, recode_integer.h, the version-header template, export map and MIT
license are byte-identical to v1.5. Existing structs/enums/signatures did not change.
Cross-minor binary substitution is not promised.

Old source was extracted without modification:

```sh
mkdir -p /tmp/recode-v16-old-consumers/v10 /tmp/recode-v16-old-consumers/v15
git archive v1.0.0 examples | tar -x -C /tmp/recode-v16-old-consumers/v10
git archive v1.5.0 examples | tar -x -C /tmp/recode-v16-old-consumers/v15
```

For GCC and Clang, each old embed/rules/data-rules consumer and the v1.5 batch-rules
consumer was configured, built and run against that compiler's relocated static
and shared 1.6 prefixes. All 28 builds/runs passed. For example:

```sh
cmake -S /tmp/recode-v16-old-consumers/v15/examples/batch-rules \
  -B /tmp/recode-v16-old-consumers/v15-gcc-ON-batch-rules \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/recode-v16-verification/gcc-release/embedding-ON/relocated
cmake --build /tmp/recode-v16-old-consumers/v15-gcc-ON-batch-rules --parallel 2
/tmp/recode-v16-old-consumers/v15-gcc-ON-batch-rules/batch-rules
```

The source-consumer CSV retains each configuration's result. This demonstrates
compatible-major package discovery and rebuilding, not old-binary compatibility.

## Performance and review

The [performance report](performance-1.6.md) identifies exact baseline/candidate
revisions, common harnesses, raw samples, medians/quartiles, code/disassembly counts,
compiler costs, cold/warm/API/kernel boundaries, RSS and executable ledgers. Both
installed optional LLVM/MIR comparison harnesses passed all 79 CTests. Seven
alternating rounds cover six workloads, six cold/scalar counts, all seven batch
sizes, short-lived contexts and cache pressure. Dedicated CPU 0..3 thread runs
retain an additional repeat after unfavorable initial concurrent medians.

Batch gains are about 11..32% at 128 records. Single-call and compilation regressions,
short-lived-context cost and inconclusive multi-thread ordering remain explicit.
LLVM/MIR warm throughput is still faster. There is no perf hardware-counter data,
fused native batch entry, SIMD backend or shared code arena. Tiny functions still
consume a page-rounded mapping; peak RSS is a separate OS observation.

Post-commit scans and next used the required isolated C-support fork at
5179f6ba61dca2050813a1aa7e2f71ba7e4ce765. The initial full scan completed with
jscpd failures; later objective scans omitted unavailable slow duplication checks
and excluded only generated build trees/tool environments. Source was not excluded.
Relevant candidates were inspected:

- C import/coverage heuristics miss CMake linking and call the tested emitter,
  runtime and diagnostic executables orphaned/untested. Direct tests and consumers
  provide contrary execution evidence.
- Signature heuristics mistake if for a function and compare separate static test
  helpers across files. These are C parsing/namespace false positives.
- Existing runtime/test file-size flags and repeated mapping rollback sequences
  are maintainability observations, not established behavioral defects. Splitting
  ownership/locking or creating a generic emission framework is outside this patch.
- The unsafe-string candidate is bounded fgets followed by sscanf %ld into a long,
  with no string destination.
- Clang's CFG fact warning also reproduces on v1.5. Its trace stops initialization
  loop unfolding before following a validated edge; all facts are initialized.
  The emitter analyzer reports no warning. No warning was suppressed.

The completed source diff was reviewed for operand aliases, spills, joins/backedges,
REX/SETcc encodings, relocations, bounds-fault ordering, stack/ABI state, plan
lifetime, native pins, counter saturation, rollback and benchmark boundaries.
No unresolved correctness defect was confirmed by these checks. Tool scores,
limited C parsing and passing tests do not prove correctness. Maintainer review,
ordered merges and separate post-merge archive/release authorization remain.
