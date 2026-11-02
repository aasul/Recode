# v1.5.0 candidate verification

Integrated implementation/package source: 928f99883d65f44b13bae7423ccd9237fa496723.
The baseline v1.0.0 tag is 35028f5bc2167e6662e67802099c7d6c5d4dcce2. Subsequent
commits add benchmark-only bulk comparisons and documentation/results; runtime,
headers, core library build/test rules and examples match the tested integrated source.
The candidate is prepared for review, not approved for release. No v1.5 tag,
merge or GitHub release was performed.

## Archive and compiler matrix

```sh
git archive --format=tar 928f998 | gzip -n > /tmp/recode-v15-candidate.tar.gz
mkdir /tmp/recode-v15-source
tar -xzf /tmp/recode-v15-candidate.tar.gz -C /tmp/recode-v15-source
sh /tmp/recode-v15-source/scripts/verify-release.sh /tmp/recode-v15-verification
```

The archive's deterministic gzip SHA-256 is
`42c76d317bbfea3a8f4d82add487965e4ccd57c64a31528c8f84df4f9de92454`.
A second independent git-archive/gzip operation produces the same hash. Its 198
regular files match that exact tracked revision, with no Git directory, build,
virtualenv or quality state. This checksum identifies the tested candidate archive,
not a future tag/release asset or later documentation/result commits.

Intel N150, Linux 7.3.0-4-cachyos x86-64, glibc 2.44, GCC 16.2.1, Clang 23.1.4.
The script was run from the Git-free archive. Actual results:

| Configuration | Result |
| --- | --- |
| GCC Release static | 14/14 CTests passed |
| Clang Release static | 14/14 passed |
| GCC Release shared | 14/14 passed, including exact exports and SONAME |
| Clang Release shared | 14/14 passed, including exact exports and SONAME |
| GCC Debug ASan/UBSan | 14/14 passed |
| Clang Debug ASan/UBSan | 14/14 passed |
| GCC Debug TSan | 7/7 focused batch, memory, integer, stability, trace, adaptive and failure suites passed |

ASAN_OPTIONS=detect_leaks=0 and UBSAN_OPTIONS=halt_on_error=1 match CI. These are
C instrumentation checks; emitted instructions are not sanitizer-instrumented.
Native guard-page reads, independent differential results and assembly ABI
sentinels provide separate evidence. No proof of security or race freedom is claimed.

The optional Clang libFuzzer harness was built from the same archive with
-DRECODE_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF. A copy of the
seed corpus ran `recode_fuzz CORPUS -runs=100000 -max_len=100` under the same ASan/UBSan
environment: 100,000 executions completed without a reported finding. This bounded
smoke is not exhaustive fuzz coverage. Failure injection, cache pressure, native
rollback cleanup and concurrent execution remain part of the matrix above.

## Installation and compatibility

Every configuration's embedding-OFF and embedding-ON tests independently build,
install, relocate and consume the package. Applications include accumulator embed,
integer rules, bounded data-rules and new batch-rules. They use public headers and
the exported Recode::recode target; the batch consumer requests Recode 1.5.

The original v1.0 example sources were additionally extracted, without modification:

```sh
git archive v1.0.0 examples | tar -x -C /tmp/recode-v10-consumers
```

For GCC and Clang, each of examples/embed, examples/rules and examples/data-rules
was configured against each matching compiler's relocated static/shared 1.5 prefix,
compiled and executed. All twelve independent builds/runs passed. For example:

```sh
cmake -S /tmp/recode-v10-consumers/examples/embed \
  -B /tmp/recode-v10-consumers/gcc-ON-embed -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/recode-v15-verification/gcc-release/embedding-ON/relocated
cmake --build /tmp/recode-v10-consumers/gcc-ON-embed --parallel 2
/tmp/recode-v10-consumers/gcc-ON-embed/embed
```

The old consumer reports Recode 1.5.0. Same-major find_package(Recode 1.0 CONFIG
REQUIRED) still discovers the new package for rebuilding. Generated header macros
are version 1.5.0, API 1 and ABI 1.5. readelf reports librecode.so.1.5; nm's dynamic
symbol set is exactly the 30 old public functions plus rc_runtime_call_integer_batch.
Private helpers are hidden. No old function or enum value was removed/renumbered;
no existing public struct fields changed. Cross-minor binary compatibility is not
claimed. The MIT license and Linux x86-64-only platform contract remain.

## Performance and methodology checks

The [final study](performance-1.5.md) compares exact immutable v1.0 and integrated
v1.5 archives, with separate installed consumers, actual LLVM ORC/MIR execution,
three alternating rounds, all six requested call counts, cold/warm/cost/RSS fields,
64 distinct context lifetimes and 4,096 retained-runtime registration/removal cycles.
All 43 comparison CTests pass against both libraries. The additional common bulk
comparison has 79 passing Release checks, and 79 passing Clang ASan/UBSan harness
checks; the latter instruments the harness while the linked Release library was
separately covered by the full instrumented-library matrix above.

The bulk consumer was archived from a008a1bb2579bf9c17bb4e7d6f6e0eea3a8aa818 and
built independently against 928f998. It compares the same six workload definitions
using real Recode batches and native LLVM/MIR/C loops. Separate installed batch-rules
and internal kernel/thread diagnostics ran from the integrated archive at all seven
requested batch sizes. Raw observations, unfavorable samples and environment records
are committed. Compiler dependencies are optional, separately built and not vendored.

Adversarial review corrected the early benchmark's N+1 cold evaluation convention
and the inherited ru_maxrss launcher floor. The original raw data are retained and
marked; the final harness checks exactly N cold managed invocations before warmup,
records cold tier counters, and measures VmHWM since exec for process RSS. Native
code bytes and tracked executable pages remain separate from RSS. MIR text size and
LLVM/MIR executable allocation totals remain explicitly unmeasured. Invalid-view
error protocol differences are excluded from cross-engine timing, not concealed.

Measured trade-offs include higher v1.5 compilation medians, some interpreter/scalar
regressions, one-item batch overhead, cold/adaptive batch losses, and larger caller
metadata making very large record batches use more RSS than MIR. No universal
speedup or general advantage over optimized C/LLVM/MIR is claimed.

## Source review and tooling

Completed implementation commits were followed by the pinned C-support desloppify
fork at 5179f6ba61dca2050813a1aa7e2f71ba7e4ce765 in its isolated Python environment,
objective scan, next, source inspection and rescan. Only actual build directories
and the tool virtualenv were excluded. The initial full scan's two jscpd detectors
failed; follow-up scans skip slow duplicates. cppcheck is not installed, and C
linkage/test recognition is incomplete. Untested/orphaned CMake modules, file-local
signature collisions (including `if`) and numeric-only sscanf into a bounded long
were inspected as false positives. Existing large-file advisories did not identify
a reproduced correctness defect. No code was changed or excluded to raise a score.

GCC -fanalyzer and Clang --analyze produced no diagnostics on the changed emitter,
runtime and comparison main. Strict compiler diagnostics, guard/ABI/differential,
allocation/mapping failure, partial-prefix, alias, pinned eviction and multithreaded
checks are separate evidence. Review traced control-flow joins, signed disp8/disp32
boundaries, branch relocations, native register preservation, mapping ownership,
failed publication, full-batch pins, status offsets and saturating accounting.
No independent human review is claimed.

## Review and release state

The benchmark, optimization and batch PRs have passed GitHub Actions:
[benchmark](https://github.com/aasul/Recode/actions/runs/41480069279),
[optimization](https://github.com/aasul/Recode/actions/runs/41481141439),
[batch](https://github.com/aasul/Recode/actions/runs/41484772684).
The release preparation head e25cb59 also passed
[all GitHub Actions jobs](https://github.com/aasul/Recode/actions/runs/41491846980).
The final documentation/result archive of e25cb59 passed a fresh GCC shared build
and all 14 tests, including relocated consumers. Its deterministic gzip SHA-256 is
`0bf80b2b5a9c52b8f3b3c6cdbc4eda4ce328c0f690de6185df3bdbc7b80eb6d1`.

The four review branches are published as [benchmark #26](https://github.com/aasul/Recode/pull/26),
[optimization #27](https://github.com/aasul/Recode/pull/27),
[batch #28](https://github.com/aasul/Recode/pull/28) and
[release #29](https://github.com/aasul/Recode/pull/29), with bases main,
bench/v1.5-jit-comparison, perf/v1.5-runtime-optimization and
feat/v1.5-batch-execution respectively. This final record update changes only
documentation; live CI on the current PR head is checked again after publication.
No source correctness blocker was found by the executed checks. Maintainer review,
ordered merges, post-merge archive verification and explicit tag/release authorization
are still required. Any final documentation-only archive has a different checksum;
verify the intended merged release archive again before publication.
