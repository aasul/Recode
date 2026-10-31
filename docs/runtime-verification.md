# Runtime milestone verification

Measurements and local checks below were run on 2026-10-31 on Linux x86-64,
Intel N150, four cores, reported frequency range 700 to 3600 MHz. GCC was
16.2.1 20260810, Clang 23.1.4, CMake 4.4.5, and the page size was 4096 bytes.
Affinity, frequency and scheduling were not controlled. These are synthetic
function results, not evidence of improvements in existing applications.
The benchmark tables record commit cc369da, before the peak-accounting follow-up.
That follow-up reports temporary mappings from failed compilation attempts;
it does not change the interpretation or native execution paths.

## Build and correctness checks

Release and ASan/UBSan builds with both compilers pass all six CTest cases:
adaptive_failures, adaptive, control_flow, failures, translator and demo.
The compiler warnings remain errors. Reproduce with:

```sh
cmake -S . -B build-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-gcc -j2
ctest --test-dir build-gcc --output-on-failure
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build build-clang -j2
ctest --test-dir build-clang --output-on-failure
cmake -S . -B build-sanitize-gcc -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_SANITIZE=ON
cmake --build build-sanitize-gcc -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize-gcc --output-on-failure
cmake -S . -B build-sanitize-clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DRECODE_SANITIZE=ON
cmake --build build-sanitize-clang -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize-clang --output-on-failure
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_THREAD_SANITIZE=ON
cmake --build build-tsan -j2
ctest --test-dir build-tsan --output-on-failure -R adaptive
```

The two focused runtime cases also pass under GCC ThreadSanitizer. Generated
instructions are not instrumented by any sanitizer. They access no shared mutable
data; TSan checks synchronization and ownership in the C runtime and test drivers.
This is evidence about exercised paths, not a proof of thread safety.
LeakSanitizer was disabled for the host tracing limitation described in
[verification.md](verification.md). Runtime failure tests independently count
wrapped heap allocations and live mapping lengths, requiring zero outstanding
allocations and mappings after cleanup. They do not account for allocations
inside system libraries or replace a complete leak detector.

The original directed instruction, guard-page, decoder fuzz, arithmetic boundary,
optimizer, relocation, and failure tests remain. The arithmetic suite now checks
the public interpreter against independently computed results for all imm8 values,
imm32 boundaries, CONST zero extension, and 1,000 deterministic straight-line
programs with 32 inputs. The control-flow suite checks interpretation and both
runtime optimization modes against its separate source-byte interpreter, original
native code, and both low-level JIT modes. It includes 1,000 deterministic forward
CFGs with 16 inputs and 200 bounded countdown programs. Branch/flag behavior,
multiple returns, backward JMP/JZ/JNZ, MOV preserving ZF, and wrapping are covered.
Native randomized comparisons run in subprocesses with alarms. Nonterminating
native and interpreted loops are each killed by a one-second child alarm;
unlimited interpretation has no hidden instruction cap. Positive diagnostic
budgets and output preservation on exhaustion have separate assertions.

Runtime-specific cases cover:

- Exact tier counts, sampled timing counts, alias reuse, source-buffer mutation,
  alternate equivalent encodings, distinct translations and stale handles.
- One-page LRU pressure over 4,000 calls, byte-below-page limits, multi-page
  unoptimized output, invalidation, cleanup, and 1,000 register/call/remove cycles.
- Six concurrent callers with 60,000 calls, simultaneous cache clearing and alias
  registration/removal, budget invariants and exact completed counts. A separate
  six-caller race asserts one shared compilation when capacity is available.
- A barrier in the wrapped clock stops a worker after lookup and pin acquisition,
  outside the cache mutex and before native entry. Removal, invalidation, clear
  and destruction return BUSY as specified. Another translation cannot evict the
  pinned mapping and falls back after capacity failure. Execution resumes and
  cleanup succeeds. An interpreted in-flight call separately protects its IR.
- Every observed registration and compilation allocation site in each build,
  discovered dynamically. Both malloc and compiler-generated calloc calls are
  intercepted. Mutex initialization, mmap, mprotect, munmap, post-mmap emission
  failure, failed rollback and failed eviction are injected. Ownership and exact
  page accounting survive failures; release retries and later compilation work.
- Fake-clock adaptive decisions: warmup, profitable compilation, cost/reuse
  rejection, low recent frequency, slower-native retirement, cooldown, timer
  failure, and bounded retry attempts. These policy tests do not depend on host
  timing or assume the policy guarantees a speedup.

GCC `-fanalyzer` and Clang's static analyzer complete without diagnostics for
all seven library translation units:

```sh
mkdir -p /tmp/recode-v03-analysis
for file in src/ir.c src/decoder.c src/optimizer.c src/codegen.c src/runtime.c src/interpreter.c src/adaptive.c; do
    gcc -std=c11 -Iinclude -Isrc -pthread -Wall -Wextra -Wpedantic -Wconversion \
        -Wshadow -Wstrict-prototypes -Werror -fanalyzer -c "$file" \
        -o "/tmp/recode-v03-analysis/$(basename "$file" .c).o" || exit
done
clang --analyze -std=c11 -Iinclude -Isrc -pthread -Xanalyzer -analyzer-output=text \
    src/ir.c src/decoder.c src/optimizer.c src/codegen.c src/runtime.c src/interpreter.c src/adaptive.c
```

## Benchmark method

```sh
timeout 60 ./build-gcc/recode_runtime_bench > runtime.csv
timeout 60 ./build-gcc/recode_runtime_bench --pressure > pressure.csv
timeout 60 ./build-gcc/recode_bench
```

The runtime benchmark uses the GCC Release build (`-O3 -DNDEBUG`, C11, warnings
as errors), CLOCK_MONOTONIC wall time and five rounds with rotating mode order.
Modes are direct optimized C, unoptimized native translation, optimized native
translation, runtime interpretation, fixed-threshold runtime and adaptive runtime.
Each mode starts with a fresh runtime/code object. Default thresholds and sampling
are used. Inputs cycle through 0 to 31, and every timed checksum is checked against
an independent C result. Invocation counts are 1, 32 and 100,000. Timed execution
includes first-call profiling, any tier transition and cache lookup. Reported total
also includes runtime creation/registration or native compilation, but excludes
cleanup and later statistics queries. Direct C has neither registration nor JIT.

Three workloads return the same values in every mode:

- arithmetic64: INPUT, 32 ADD 7/SUB 6 pairs, RET; result x + 32.
- identity: INPUT, RET; result x.
- loop32: CONST 32, SUB 1/JNZ until zero, RET; result zero.

The direct baseline is an indirect call through a volatile function pointer.
Objdump confirms its bodies are MOV/RET, LEA/RET and XOR/RET respectively; the
caller still executes calls and accumulates their results. In particular, direct
C can remove the countdown loop completely. These are equivalent-result baselines,
not identical instruction streams or a comparison of identical CPU work.
No claim of loop optimization or instruction-count-derived speedup is made.
The original translator benchmark supplies additional branch/flags workloads.

Registration/decoding time is averaged over 200 register/remove iterations,
excluding removal. Native compilation latency averages 200 compilations per mode,
excluding release. Single-call transition probes time calls 1, 32 and 33, with
statistics outside timing. Small-count timings are strongly affected by timer,
allocator and scheduling noise and should not be treated as stable estimates.

## Observed runtime costs

Median ns/call over five 100,000-call rounds; parentheses show min-max:

| Workload | Direct C | Native off | Native on | Interpret | Fixed | Adaptive |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic64 | 1.433 (1.349-2.393) | 11.955 (11.178-19.319) | 2.855 (2.609-3.094) | 151.557 (148.799-176.169) | 33.856 (31.784-37.525) | 34.193 (33.538-39.570) |
| identity | 1.410 (1.361-1.530) | 2.443 (2.335-2.521) | 2.420 (2.291-2.516) | 51.957 (45.594-53.791) | 36.399 (28.061-38.091) | 39.899 (34.349-51.433) |
| loop32 | 2.275 (1.941-2.420) | 23.014 (19.715-23.827) | 22.595 (14.747-23.822) | 149.895 (144.230-151.602) | 48.930 (42.483-52.684) | 48.149 (42.322-53.291) |

Tiering helps relative to interpretation in the hot arithmetic/loop cases, while
all runtime modes cost substantially more than direct C and low-level native
calls. Optimizing identity is effectively neutral within observed variation.
Native runtime lookup, locking and profiling together add tens of ns over a raw
function call. This benchmark does not isolate mutex, timer and lookup costs from
one another. Adaptive identity sometimes declines until late in the workload:
measured interpreted counts ranged from 31 to 88,192 of 100,000. Its performance
is consequently less stable than a fixed decision. Coarse estimates remain
sensitive to noisy tiny-function timings.

All five hot arithmetic and loop rounds interpreted 31 calls then executed
99,969 native calls, with 99,968 hits and 32 misses (99.968% hit rate). Each used
one 4096-byte mapping, 600 persistent metadata bytes, and 528/32 IR bytes
respectively. Compilations on the arithmetic adaptive path ranged from 4.969 to
17.833 us; loop compilations ranged from 10.455 to 27.590 us. Clock/scheduler and
mapping placement variation were not isolated.

Median total microseconds for short workloads, including registration/compilation:

| Workload | Calls | Direct C | Native off/on | Interpret | Fixed | Adaptive |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic64 | 1 | 0.076 | 9.365 / 8.237 | 2.072 | 1.802 | 1.706 |
| arithmetic64 | 32 | 0.161 | 11.101 / 9.183 | 10.567 | 17.934 | 18.972 |
| identity | 1 | 0.071 | 4.188 / 4.206 | 0.623 | 0.323 | 0.276 |
| identity | 32 | 0.102 | 4.264 / 4.229 | 2.575 | 7.026 | 2.394 |
| loop32 | 1 | 0.055 | 4.659 / 4.724 | 0.864 | 0.625 | 0.492 |
| loop32 | 32 | 0.146 | 5.336 / 5.484 | 5.940 | 10.630 | 10.672 |

Fixed/adaptive compilation is harmful at 32 calls for arithmetic and loop32 in
this run. Adaptive avoids that cost for short identity. Differences among the
three first-call runtime modes mostly show measurement variation; all interpret
that first call. Registration averages were 859.7, 126.0 and 197.1 ns for
arithmetic, identity and loop32. Low-level compilation off/on averaged
6.354/9.032, 3.800/3.913 and 4.222/4.447 us. Emitted sizes off/on were 392/14,
8/8 and 22/22 bytes. A fixed transition probe measured call 32 at 19.973,
14.214 and 18.882 us, versus following cached calls at 143, 146 and 163 ns.
Those individual probes include timer cost and are not steady-state medians.

An earlier run with immediate average-based native retirement showed arithmetic
adaptive rounds as high as 144.475 ns/call after isolated timing outliers retired
code. The implementation now requires eight consecutive slower native samples
and a 25% margin before retirement. The final table above uses that change.
This reduces the observed failure mode but does not make the estimator precise.

## Memory pressure

Pressure mode registers distinct CONST/RET functions, each using one page despite
emitting ten bytes, and makes three calls per registration: interpretation,
compilation and one cached hit. It inserts `budget / page_size + 32` functions.
The following separate run did not overlap local compiler/test jobs:

| Budget MiB | Functions | Peak mapped bytes | Metadata bytes | IR bytes | Total ms | Registration ms | Execution ms | Cleanup ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 1,056 | 4,194,304 | 321,320 | 16,896 | 10.482 | 2.922 | 7.499 | 2.045 |
| 16 | 4,128 | 16,777,216 | 1,255,208 | 66,048 | 53.355 | 31.304 | 21.855 | 8.497 |
| 64 | 16,416 | 67,108,864 | 4,990,760 | 262,656 | 1398.012 | 1263.640 | 133.442 | 38.878 |

Each budget produced exactly 32 evictions, one hit and two misses per function,
and returned mapped accounting to zero after clear. Destruction released all IR
and metadata. Persistent metadata/IR are requested bytes; they exclude allocator
headers, transient CFG/decoder storage and process RSS. Peak mapping statistics
and failure wrappers validate accounting, not physical resident memory usage.
Linear translation matching dominates large registration sets and is a known
scaling regression. A separate pressure run overlapping builds took 3398.854 ms
at 64 MiB, illustrating sensitivity to competing work; it was not used for the
table above. No mapping budget was exceeded in either run.

## Adversarial inspection

A separate pass traced lifetime and failure paths after implementation, looking
for stale pointers, collisions, duplicate compilation, undercounted mappings,
state changes on failed operations, and misleading performance comparisons.
In particular:

- Pins are acquired before unlock and dropped only after native return. Removal
  cannot free an active handle; aliases retain shared IR. There is no unlocked
  use of a handle after its active count drops.
- Invalidation with a failed unmap must disable native lookup while retaining
  budget usage. Failed generation plus failed rollback has the same ownership
  rule. Global cache error statistics retain deferred cleanup errors.
- Preparation/query failures allocate no code; post-mmap failures retain the full
  rounded length. Emission size and mapping usage are separate quantities.
  Review found that a successful rollback omitted its temporary mapping from
  peak usage. A follow-up tracks whether mmap succeeded even after rollback,
  and tests distinguish failed mmap from failed protection plus successful cleanup.
- Runtime destruction rejects accepted calls and requires external exclusion of
  new API entry. It cannot detect a thread that has not acquired the mutex, so
  callers must join threads before final free. This is an explicit API limit.
- Identity compares fields of owned unoptimized IR; compiler mode is runtime-local.
  No mutable caller pointer, weak hash, raw cache pointer or native pointer is
  used as public translation identity.
- Benchmarks check equal returned values and actual calls, disclose the compiler's
  shorter direct baseline, include compilation in total cost, and report cold
  regressions instead of presenting only hot kernel timings.

The review retained targeted tests for these invariants. It also added multi-page
rounding, interpreted in-flight teardown, mutex initialization failure and heap
cleanup accounting checks. No unsupported claim of general architectural
compatibility, guaranteed profitability or full leak freedom follows from these
checks.

## Post-commit desloppify review

Reused the isolated Python 3.14 environment installed directly from
`https://github.com/aasul/desloppify.git@feat/c-native-analysis`. Its installed
`direct_url.json` records commit 5179f6ba61dca2050813a1aa7e2f71ba7e4ce765.
Read the installed `desloppify/data/global/SKILL.md` before inspecting findings.
No upstream main or PyPI substitution was used.

```sh
PATH="$PWD/.venv-desloppify/jscpd/node_modules/.bin:$PATH" \
    .venv-desloppify/bin/desloppify --lang cxx \
    --exclude .venv-desloppify --exclude build-gcc --exclude build-clang \
    --exclude build-sanitize-gcc --exclude build-sanitize-clang --exclude build-tsan \
    scan --path . --no-badge
.venv-desloppify/bin/desloppify --lang cxx next
.venv-desloppify/bin/desloppify --lang cxx show '*' --status open
```

The scan and the rescan after the peak-accounting fix both report 32 open items:

| Items | Inspection result |
| --- | --- |
| 10 orphaned files | CMake links all library translation units and defines the three executable entry points. C linkage through header declarations is missing from the tool's importer graph. |
| 9 untested modules | CTest directly exercises the library and demo; benchmark executables were run separately with result assertions. The importer-based report misses these callers. |
| 2 unreachable-code reports | `failed:` labels in decoder.c and ir.c are reached by goto. Error and allocation-failure tests exercise them. |
| 6 stale exclusions | Only the isolated environment and generated build trees are excluded. No project source is excluded. |
| 3 structural advisories | adaptive.c keeps one ownership/mutex model in 574 lines; recode.h contains public declarations; the 572-line control-flow suite shares its independent byte interpreter and native fixture. Inspection found no defect requiring a split. |
| 1 signature variance | The purported function is the C keyword `if`, a parser false positive. |
| 1 boilerplate duplication | Both benchmarks deliberately construct the same 64-operation arithmetic input. Keeping that short fixture local avoids coupling the two executable drivers. |

No scanner finding was marked fixed or hidden to change its score. Manual
ownership review during this phase found and fixed the temporary-mapping peak
statistic described above. All four compiler/sanitizer configurations, both TSan
runtime cases, and the affected GCC/Clang analyzer checks passed after that fix.
Both runtime benchmark modes were rerun after it; all checksum, capacity and
cleanup assertions passed, including 32 evictions and zero mapped bytes after
clear for every budget.

The fork still provides incomplete C/C++ analysis. cppcheck is unavailable;
security coverage falls back to regex heuristics. There was no scored subjective
review; all 20 subjective dimensions remain unassessed (recorded as zero).
Reported overall/objective/strict/verified scores were 15.4/61.6/15.5/61.6.
Mechanical health/strict scores were file health 88.3/88.3, code quality 53.3/53.3,
duplication 98.9/98.9, test health 31.8/32.9, and security 100/100. These values
include the C discovery limitations and are not evidence of security, test
completeness or correctness. The compiler diagnostics, static analyzers,
sanitizers, failure tests and source review provide separate evidence.
