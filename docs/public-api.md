# Proposed public API freeze (v0.9)

The supported installed interfaces are recode.h, recode_integer.h and generated
recode_version.h. Functions in src headers, native entry conventions, optimizer
analysis structures and trace kernels are private. The shared library exports
only the 30 declared public functions, controlled by cmake/recode.map and checked
with nm/readelf in the shared-abi CTest. Static archives also contain private
symbols for linking; those names carry no compatibility promise.

The supported machine ABI is Linux System V AMD64, LP64, ordinary 32-bit C enums
and default structure packing. No alternate packing, short-enum, Windows ABI or
cross-architecture support is implied. Compile with GCC or Clang in C11 mode.
Public structs are caller-allocated values; their binary layout matters. Opaque
builder, finalized-program and runtime allocations belong to the library.

The proposed 1.x policy is source compatibility for documented uses within major
version 1. Minor releases may add operations/functions and extend value structs,
requiring consumers to rebuild. Rebuild with matching headers and library for
each major/minor. Binary compatibility is limited to patch releases within the
same major/minor on the supported ABI. Patch releases must preserve layouts,
function signatures, symbols, enum values and ownership semantics. No public
struct fields may be inserted in a patch release. Unknown future enum values must
be handled by a default/error path. Fixes of undocumented or erroneous behavior
are permitted, with migration notes when observable.

SONAME is librecode.so.<major>.<minor>, preventing silent substitution by a
layout-changing minor release. The current proposed ABI has SONAME 0.9; the first
stable ABI will be 1.0. RECODE_ABI_MAJOR/MINOR identify matching header ABI;
RECODE_API_VERSION remains 1 because existing source contracts are preserved.
rc_version_string() identifies the linked implementation. Experimental 0.x CMake
requests require the same minor. v1.0 will accept compatible-major CMake source
requests; this is package discovery for rebuilding, not loader ABI compatibility.
No existing pre-1.0 binary ABI is retained by this freeze.

## Ownership and concurrency

| Object | Creation/output | Lifetime and synchronization |
| --- | --- | --- |
| Decoded rc_program | rc_decode, empty struct required | Owns allocated insns; rc_program_free releases them. Borrowed IR arrays can validate/interpret/emit but must not be freed by this function. Exclude mutation during readers. |
| rc_code | rc_compile, empty struct required | Owns a low-level mapping; exclude calls before rc_code_free. Failed rollback can return a non-callable mapping requiring retry cleanup. |
| Builder | rc_builder_create, NULL pointer required | Mutable, caller excludes concurrent access. Append failure retains previous instructions. Destroy independently of finalized programs. |
| Integer program | rc_builder_finalize, NULL pointer required | Immutable copy; safe concurrent readers. Destroy only after readers finish. Registration copies it. |
| Runtime | rc_runtime_create, NULL pointer required | Config copied and immutable. Operations synchronize internally. Stop new API entry and join callers before destruction; include threads waiting for a lock. |
| Handle | successful registration | Nonzero, scoped to its runtime, never reused there. Do not transfer between runtimes. Aliases share translations, not handle lifetime. |
| Memory view | supplied at call | Borrowed descriptor and readable bytes; no writes/destruction until return. Independent views can execute concurrently on one handle. |

Do not forge opaque pointers, use stale pointers/handles, copy ownership, overlap
input/output/error storage, cancel executing threads, use APIs from signal handlers,
or reuse a runtime after multithreaded fork before exec. A supported handle lookup
can fail with NOT_FOUND; using freed runtime/program pointers is caller misuse.

Removal returns BUSY for an active handle. Invalidation/clear protect pinned native
code, including trace snapshots and interpreter continuation. Failed release retains
ownership for retry; no function from a failed native publication is callable.
Successful destroy sets the runtime pointer to NULL. Destruction cannot race any
other API entry. See embedding.md, runtime.md and memory-views.md for details.

## Execution and failures

Accumulator input has one uint64_t argument. Integer programs have 0..2 uint64_t
arguments, 1..64 registers, and 1..4096 instructions. Exact arity is required.
Integer values use modulo-2^64 arithmetic and explicit comparisons; pointers are
not integer arguments. Loads read unsigned little-endian data through a view.
Programs can loop forever. Only direct interpreters accept a diagnostic instruction
budget; managed/native calls have no deadline. Terminating one native call safely
requires application process isolation, not a thread timeout.

INTERPRET never compiles. FIXED and ADAPTIVE permit whole-function compilation;
compilation failures retain interpretation and populate last_compile_error.
TRACE is supported only for accumulator programs. No integer operation silently
falls back while being advertised as native or trace support. Bounds errors remain
execution failures after compilation fallback, with result unchanged. Statistics
count attempted execution, including bounds errors, and are synchronized snapshots.

| Status | Meaning |
| --- | --- |
| OK | Successful operation; optional error cleared |
| ARGUMENT | Invalid argument/descriptor, wrong representation/arity or nonempty creation output |
| MEMORY | Allocation failure or allocation-size overflow |
| TRUNCATED, UNSUPPORTED | Incomplete or unsupported byte encoding; unsupported representation/policy |
| UNINITIALIZED, UNDEFINED_ZF | Use before definition on an incoming path |
| MISSING_RET, TRAILING | Escaping fallthrough or unreachable instructions; closed loops are still permitted |
| INVALID_IR, BRANCH | Invalid operation/operand/IR target, or byte branch outside input/into an instruction |
| CAPACITY | Emission buffer, branch displacement, executable budget or accounting/handle capacity |
| SYSTEM | OS failure; system_errno supplies the underlying errno/pthread error |
| BUSY, NOT_FOUND | Active resource or unknown handle |
| BUDGET | Direct interpreter instruction limit exhausted |
| BOUNDS | Executed read outside declared data view |

Execution failures preserve results. Failed creation/registration preserves outputs,
except low-level compile's documented retained mapping on failed rollback.
Emitters set queried length on capacity failure and leave byte storage unchanged;
rc_emit resets length to zero before validation. Integer emitter internals are
private. rc_optimize mutates a caller-owned program only after successful analysis.
NULL error is supported. Destroy/free with NULL is allowed only where the header
explicitly says so; rc_code_free(NULL) and rc_runtime_destroy(NULL) are errors.
