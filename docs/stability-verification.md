# v0.9 stability verification

Audit scope covered decoder lengths/branch boundaries, accumulator CFG and ZF
liveness, integer must-analysis convergence, wrapping/shifts/comparisons, bounded
loads, native layout and rel32 targets, System V registers/stack frames, trace
guards/snapshots/continuation, cache identity, mutex ownership, pins, eviction,
rollback and public lifecycle/error behavior. No architecture replacement was needed.

The private integer emitter previously indexed its encoding-size table and offset
array using unchecked internal descriptors. Although public finalized programs
satisfied those preconditions, malformed private input could access beyond them
or select a mismatched memory-entry ABI. It now uses the same complete validator
before any emission. Validation checks signature/count/storage, every instruction,
target/fallthrough/reachability/definitions and memory-entry consistency.
Regressions cover invalid opcode/count/registers/ABI flags and untouched outputs.
This is internal boundary hardening, not a claim that valid public construction
could bypass finalization. Compilation now performs two additional bounded
validation allocations; allocation failures retain interpreter fallback, including
failed post-mapping emission and failed rollback cleanup.

The library exported internal CFG, trace and integer helpers in shared builds.
cmake/recode.map now exposes only the 30 installed API functions. The shared-abi
CTest checks exact exports and major/minor SONAME. Private diagnostics link an
uninstalled static copy; public tests and installed consumers exercise the actual
shared library. On the local glibc system readelf reports only libc.so.6 as an ELF
dependency and librecode.so.0.9 as SONAME. There is no cross-minor binary promise.
See public-api.md for the proposed freeze and memory-limits.md for quota analysis.

Normal GCC/Clang Release, both Debug ASan/UBSan and GCC shared Release pass all
13 CTests. Static has the dispatch probe; shared substitutes the symbol/SONAME
check. All three consumers build/run after static/shared prefix relocation.
GCC TSan passes memory/integer/stability/trace/adaptive/adaptive_failures.
The new stability test performs 96,000 calls, 24,000 concurrent registrations,
aliasing, invalidation and removal across four runtime lifetimes, requiring zero
live mappings/program bytes/active calls at shutdown. Existing cache-pressure,
active-pin, registration-allocation, mapping/protection/release and deterministic
differential suites remain. Wrapped offset arithmetic and emitter failure
regressions were added. ASan leak detection is disabled; generated code is not
sanitizer-instrumented. Test commands follow application-verification.md, with
/tmp/recode-v09 for the primary build and -DBUILD_SHARED_LIBS=ON for shared tests.

Optional Clang libFuzzer completed 100,000 bounded inputs without a failure after
harness validation. See fuzzing.md for exact commands and seed format. Native
execution is limited to acyclic programs; loops use budgeted interpretation.
CI retains all four compiler/sanitizer jobs and adds shared-library/fuzz smoke.
v0.8 PR #19's initial head passed the four original CI jobs on Ubuntu runners.

Full-source Clang --analyze and GCC object -fanalyzer were run on all ten library
translation units. One Clang facts-loop warning and eight GCC interpreter
uninitialized-register paths remain: the analyzer does not establish that the
entire facts array is initialized before propagation or that opaque programs have
passed definite-assignment validation. Manual path inspection confirms every node
is initialized in the completed first loop, targets are checked before use, and
public programs cannot read undefined registers after finalization. The rejection,
loop/join and differential suites exercise these invariants. No warning was
suppressed and registers were not zeroed to conceal invalid reads. Other units,
including the native emitters and synchronized runtime, reported no diagnostics.

| Environment | Verification |
| --- | --- |
| Linux 7.3.0-4-cachyos x86-64, Intel N150 | GCC 16.2.1, Clang 23.1.4; Release, ASan/UBSan, GCC TSan, static/shared |
| Ubuntu GitHub Actions x86-64 | PR-specific results linked in final PR descriptions; no extrapolation to every Linux distribution |
| Other architectures/ABIs, non-Linux | Unsupported and rejected by CMake |

No formal verification or independent human security review is claimed. No known
release-blocking source defect was identified by this audit; later release
validation still must complete. The incomplete C-support quality review and its
limitations are recorded after committing the implementation.

Performance used the v0.8 environment and five rotating samples per cell, pinned
to CPU 0 with no concurrent local builds/tests. Compilation includes size query,
emission validation, mapping and RX transition. Mixed inputs, arithmetic:

| Measurement | v0.8 | v0.9 |
| --- | ---: | ---: |
| raw compile ns | 8345.000 | 20835.000 |
| direct interpreter ns/call | 27.336 | 29.102 |
| managed FIXED ns/call | 40.510 | 58.567 |
| FIXED 32-call total us | 17.222 | 7.921 |

Native validation adds bounded compile-time work. Timing varies, and the observed
v0.8 interpreter regression remains disclosed; no performance fix is claimed in
this hardening milestone. Full steady-state distributions/min/max are retained
in benchmarks/results/v09-integer.csv.
