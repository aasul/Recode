# Recode 1.0.0 release notes

Recode 1.0.0 is the first stable release. The v0.8, v0.9 and v1.0
preparation changes are merged into main.

Recode is a C11 execution library for Linux x86-64 only. Applications construct
bounded integer rules or translate the documented accumulator instruction subset.
It does not optimize arbitrary binaries and provides no sandboxing.
It supplies validation, interpretation, whole-function native compilation,
call statistics and a managed executable cache. Accumulator-only tracing remains
available and can be substantially slower than whole-function compilation.

Since v0.7, integer rules can read a caller-owned byte buffer through a bounded,
read-only, little-endian view. Invalid reads return BOUNDS and preserve results.
The independent installed data-rules example executes arithmetic and conditional
filters over external records and checks C/interpreter/native equivalence.

Stability work revalidates private native-emitter inputs, hides internal shared
symbols, adds bounded optional fuzzing, concurrent lifecycle stress and failure
regressions. All 30 installed functions are covered by the public API contract.
1.x preserves documented source compatibility; consumers rebuild across minor
releases. Binary compatibility is promised only within a major/minor patch line;
the first stable SONAME is librecode.so.1.0. See public-api.md and migration.md.

Installation uses CMake and Recode::recode with libc/POSIX threads; MIT licensing
is unchanged. No accounts, telemetry, commercial dependencies or hosted service
are required. Read getting-started.md, troubleshooting.md and release-verification.md
for actual build, integration, measurement and failure behavior.

Direct optimized C is faster in the measured workloads. Expanded interpreter
dispatch regressed in v0.8; compilation validation adds cold cost. ADAPTIVE can
remain interpreted and has timing-sensitive decisions. No universal speedup is
claimed. Cold and steady measurements, variation and cache pressure are retained.

Unsupported: arbitrary binaries/ELF optimization, other platforms/ABIs, raw pointer
integer arguments, stores, external calls, syscalls, integer TRACE, host-process
isolation, total-memory quotas and native/managed execution deadlines. Recode is
not a sandbox. Callers must supply valid pointers/lifetimes, bound hostile input,
and synchronize destruction. Sanitizers do not instrument generated instructions.
