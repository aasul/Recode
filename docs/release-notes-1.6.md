# Recode 1.6.0 candidate

This branch prepares v1.6.0 for review. It does not authorize merging, tagging
or publishing a release. The baseline is v1.5.0 at
9f49d55296614cadeaadd7338b60bfac642a53fb. The tag was created during this work
and matches the implementation commit used by the earlier measurements.

Integer code uses bounded use-count register assignment, deterministic stack
homes under pressure, block-local dead pure-write removal, compact constants
and direct register operations. The arithmetic fixture shrinks from 81 to 27
bytes and loses all nine explicit stack loads/stores. Memory loads retain their
checks and original instruction offsets. No new CPU extension is required.

Public batching groups native records between the existing sampling positions.
The first batch still interprets; later batches preserve policy selection, pins,
preflight, alias restrictions, fail-fast outputs and completion/statistics counts.
Native execution still enters once per record. No fused native loop, SIMD batch
interface or unchecked public entry is added.

Compilation reuses one validated layout through RW-to-RX publication. Straight-line
validation avoids temporary CFG heap storage; branches retain the complete
reachability and definition analysis. Tiny functions still consume page-rounded
executable mappings. There is no shared executable arena or extra persistent cache.

The 31 public functions and existing public structs/enums remain unchanged.
Rebuild v1.0/v1.5 consumer sources with matching 1.6 headers and library. SONAME
is 1.6, API version remains 1, and binary compatibility is limited to the same
major/minor patch line. The MIT license and Linux System V x86-64 scope remain.

Read [performance-1.6.md](performance-1.6.md) for immutable comparisons, raw data,
separate kernel/public API costs and regressions. Read
[release-verification-1.6.md](release-verification-1.6.md) for the exact tested
candidate and compiler/package matrix. Direct C and external compilers can remain
faster in warm execution. These changes are not a universal performance guarantee.

Native machine code is not sanitizer-instrumented. Independent execution,
differential references, ABI sentinels, guard pages and failure/concurrency tests
provide separate evidence, not formal verification. Recode is not a sandbox,
arbitrary-binary optimizer or managed execution timeout mechanism. Publicly
unsupported instructions/platforms and caller lifetime obligations are unchanged.
