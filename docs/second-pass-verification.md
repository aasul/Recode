# Second-pass refactor verification

Baseline: `5bc1d183dc28f0ce27f8bffec9702cbaed6c80fc`, including the previous
runtime, CFG and integer-emission refactors in PRs #45 and #46.
Implementation commits: `f783c1e`, `86f33fc`, `d9db188`.

## Audit and implementation

- `decoder.c:rc_decode` mixed byte parsing, branch resolution and validation
  diagnostics, with a shared offset variable serving each phase. `resolve_branches`
  now owns displacement bounds and instruction-boundary lookup. Each failing
  phase records its byte diagnostic; the caller retains allocation ownership and
  the existing cleanup path. Parsing still precedes resolution, then IR validation.
- `trace.c:emit_body` combined arithmetic folding, instruction emission and the
  quantum backedge. `fold_arithmetic` and `emit_loop_backedge` separate those
  operations without changing the native layout or recorded metadata.
- `adaptive.c:trace_after` combined recording completion with hot-edge observation.
  `finish_recording` now owns compaction, publication or abort cleanup and local
  observer updates under the same mutex, with the same allocation and update order.
- The signed-imm32 predicates in `optimizer.c` and `trace.c` were equivalent.
  Both now use `rc_fits_signed32`. This tests modular arithmetic bit patterns;
  relative-branch distance validation retains its separate contract.
- Trace and integer emitters had identical count-only byte and little-endian u32
  writers. Private `emitter.h` shares those operations. Capacity validation,
  relocation and mapping policies remain in their respective emitters.

The whole-function optimizer still respects block entries and ZF liveness;
trace folding can cross recorded unconditional edges and must retain ADD 0 even
at a return. Those differences are intentional and remain separate.

The runtime keeps its private state and single mutex in `adaptive.c`. Moving
cache ownership, pins and publication into separate files would require a wider
internal interface; no such split was justified here. Public headers, layouts,
export maps, policy defaults and memory-protection transitions are unchanged.
No behavioral correction is included.

New source comments explain the shared writer's preflight contract, modular
imm32 range checks and zero-delta ZF preservation. The existing backedge comment
moves with its flag-preserving emission sequence.

## Verification

Environment: Linux x86-64 VM, Intel Xeon Platinum 8573C, three vCPUs with a
2-CPU cgroup quota, GCC 14.2.0, Clang 19.1.7 and CMake 4.4.4. Development tools
were installed outside the repository.

Baseline GCC Release and Debug ASan/UBSan each passed 16/16 CTests, with the
project's warning flags and `-Werror`. The new decoder diagnostic-precedence and
trace folding/capacity tests also passed when linked against the original archive.
The final source passed the following complete suites:

| Configuration | Build directory | CTest |
| --- | --- | --- |
| GCC Release static | `/tmp/recode-after` | 16/16 |
| Clang Release static | `/tmp/recode-clang-release` | 16/16 |
| GCC Release shared | `/tmp/recode-gcc-shared` | 16/16 |
| Clang Release shared | `/tmp/recode-clang-shared` | 16/16 |
| GCC Debug ASan/UBSan | `/tmp/recode-after-sanitize` | 16/16 |
| Clang Debug ASan/UBSan | `/tmp/recode-clang-sanitize` | 16/16 |
| GCC Debug ThreadSanitizer | `/tmp/recode-gcc-tsan` | 16/16 |

The configure/build/test commands used this pattern:

```sh
cmake -S . -B /tmp/recode-after -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/recode-after --parallel 2
ctest --test-dir /tmp/recode-after --output-on-failure
```

Clang configurations use `-DCMAKE_C_COMPILER=/tmp/recode-llvm/usr/bin/clang-19`.
Shared configurations add `-DBUILD_SHARED_LIBS=ON`. Sanitizer configurations use
Debug and `-DRECODE_SANITIZE=ON` or `-DRECODE_THREAD_SANITIZE=ON`.
ASan/UBSan tests used `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
Shared tests verify exports/SONAME; all suites include relocated static/shared
installation and five independent consumers. Embedding sub-builds use Release,
as defined by `TestEmbedding.cmake`. The dispatch probe retains its
clock, lock and allocation assertions. Failure sweeps and concurrent cache tests
remain enabled.

The installed Recode/C comparison suite passed 53/53 tests:

```sh
cmake --install /tmp/recode-gcc-shared --prefix /tmp/recode-comparison-prefix
cmake -S benchmarks/competitors -B /tmp/recode-comparison \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/recode-comparison-prefix
cmake --build /tmp/recode-comparison --parallel 2
ctest --test-dir /tmp/recode-comparison --output-on-failure
```

Optional LLVM/MIR adapters were not enabled. The existing decoder/IR/integer
libFuzzer harness completed 100,000 runs against the final library:

```sh
cmake -S . -B /tmp/recode-fuzz \
  -DCMAKE_C_COMPILER=/tmp/recode-llvm/usr/bin/clang-19 \
  -DCMAKE_BUILD_TYPE=Debug -DRECODE_BUILD_FUZZERS=ON \
  -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF
cmake --build /tmp/recode-fuzz --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/recode-fuzz/recode_fuzz /tmp/recode-corpus -runs=100000 -max_len=512 -timeout=5
```

The corpus began with `fuzz/corpus` and retained mutations between campaigns.
Direct trace tests compare RAX, ZF, PC, argument and quantum against an independent
walker. Added cases cover imm32 limits, a SUB-only representable delta,
nonrepresentable sums, zero sums and every insufficient capacity before exact fit.
A temporary trace-test driver wrote length-prefixed native bytes for 620 compiled
test traces; `cmp` found identical before/after output. Six integer profile kernels
also compared byte-for-byte. GCC Release objects for `adaptive.c`,
`integer_codegen.c` and `optimizer.c` are byte-identical to baseline.

Leak-enabled trace, adaptive-failure and control-flow checks failed because
LeakSanitizer reports that it cannot operate under ptrace. These runs do not
establish leak freedom. Sanitizers instrument C, not emitted instructions; native
ABI, guard-page and differential tests provide separate evidence.

## Performance

Both libraries use GCC Release without LTO. Unchanged trace, runtime, translator
and integer-profile drivers ran three times, alternating revision order and pinned
to CPU 0. The trace/runtime/integer pressure modes also passed their accounting
checks. No builds, tests or scanners ran concurrently with timed measurements.
CPU frequency, scheduling and other host activity were not controlled.

A temporary stage driver ran 200,000 checked operations per process, with seven
alternating before/after pairs. Decode includes validation and release. Trace
emission includes validation, sizing and writing a 65-operation trace containing
64 arithmetic instructions and a loop guard. Values are median ns/operation
(minimum-maximum across processes):

| Stage | Before | After |
| --- | --- | --- |
| decode, 64 arithmetic instructions | 724.54 (696.29-1321.84) | 696.73 (659.35-892.99) |
| decode, countdown | 182.62 (124.80-281.56) | 167.04 (118.28-223.30) |
| decode, identity | 94.47 (75.87-125.81) | 82.72 (66.51-133.88) |
| trace emit, unoptimized | 1588.43 (1455.55-3188.92) | 1572.09 (1462.69-1934.53) |
| trace emit, optimized | 1047.08 (989.25-1409.62) | 920.80 (855.29-2646.74) |

The unmodified warmed TRACE driver uses 20,000 varying-input calls per round;
three processes retain all five internal rounds (15 observations per row).
Values are median ns/call (minimum-maximum):

| Workload | Before | After |
| --- | --- | --- |
| countdown | 212.59 (169.10-430.55) | 202.55 (168.00-259.55) |
| predictable | 206.80 (178.41-436.16) | 217.17 (184.12-250.53) |
| changing | 725.13 (637.93-1060.49) | 779.80 (675.81-1779.44) |
| paths | 231.11 (197.39-449.80) | 247.46 (204.09-376.96) |
| short | 59.00 (53.15-89.53) | 64.92 (53.62-165.31) |

These distributions overlap substantially and do not establish performance parity
or a uniform speedup. Longer intermediate-candidate measurements at 2,000,000
calls also showed large variability in unchanged direct-C controls. Investigation
removed an exit-size hoist that repeatedly slowed unoptimized emission by about
8%, and retained the original trace eligibility expression to restore the exact
baseline GCC runtime object. The final patch keeps the structural extractions.

Raw final measurements, stage-driver source, reproduction scripts and summaries
are retained in the session's `/workspace/recode-evidence/verified` directory.
Build/test/fuzz/quality logs are in `/workspace/recode-evidence/logs`. Intermediate
measurements remain separate from that final directory. The scripts expect the
original `/tmp/recode-before` and `/tmp/recode-after` build directories.

## Post-commit quality review and limits

Desloppify ran and was rescanned from the repository-specified C-support fork,
pinned to `5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`:

```sh
/tmp/recode-desloppify/bin/desloppify --lang cxx scan --path . --no-badge
/tmp/recode-desloppify/bin/desloppify --lang cxx next
```

Its C/C++ model does not capture CMake linkage: it reports linked modules as
orphaned/untested, treats `if` as a function, and compares unrelated private helper
signatures across executables. Its per-file Cppcheck invocation also reports
missing project includes, unused public functions and header fields, plus style
suggestions. The two string-API candidates are bounded `/proc/self/status` reads
with numeric `%ld` conversions, not unbounded string destinations. These findings
were inspected in the actual source. Size thresholds remain; they do not justify
splitting private ownership or changing the public API. The optional jscpd stage
failed. No subjective score or complete native C coverage is claimed.

Standalone Cppcheck 2.17.1 completed all 21 `src` translation units without
warning, performance or portability findings using explicit C11/include settings:

```sh
LD_LIBRARY_PATH=/tmp/recode-llvm/usr/lib/x86_64-linux-gnu \
  /tmp/recode-llvm/usr/bin/cppcheck --check-level=exhaustive --std=c11 \
  --enable=warning,performance,portability --error-exitcode=1 \
  -Iinclude -Isrc -Ibenchmarks/competitors -Iexamples/batch-rules \
  -I/tmp/recode-after/generated src
```

The extracted Cppcheck installation needed its `cfg` lookup linked beside the
executable before the successful runs. No source suppressions or weakened tests
were added. Broader runtime file separation, optional backend verification and
leak detection on a compatible host remain outside this verified patch.
