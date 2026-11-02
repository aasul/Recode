# Migrating v1.0, v1.5 or v1.6 to v1.7

Rebuild unchanged supported source with matching 1.7.0 headers and library.
SONAME is librecode.so.1.7 and RECODE_ABI_MINOR is 7; API version remains 1.
No existing function, field or enum value is removed or renumbered. The new
configuration flag and storage-statistics fields are trailing additions under
the minor-release ABI policy. Cross-minor binary substitution is not promised.
Same-major CMake discovery continues to accept older 1.x source consumers.

The descriptor batch contract and default first-batch interpretation remain.
Native batches now fuse consecutive unsampled items. Set cold_batch_compile
explicitly to permit cost-based cold compilation and cached-alias reuse.
The new rc_runtime_call_integer_records API directly traverses fixed-stride
records; it requires disjoint output/layout/record spans and validates offsets
before execution. No conversion is required for existing descriptor callers.

Small integer translations may share immutable RX page snapshots. Executable
budget accounting includes the transient spare page, retired snapshots and
failed rollback mappings. Removing one entry does not free a shared page still
owned by others. See [slab lifetimes](code-slabs.md) and [measurements](performance-1.7.md)
for compilation and memory trade-offs. No new CPU extension is required.

## Earlier migration: v1.0 or v1.5 to v1.6


Rebuild with matching 1.6.0 headers and library. The new SONAME is
librecode.so.1.6 and RECODE_ABI_MINOR is 6; RECODE_API_VERSION remains 1.
There are no new public functions, enum values or struct fields in this minor.
Existing scalar and batch source, borrowed memory views, failure outputs,
statistics meanings, policies and ownership rules remain supported. Same-major
CMake discovery accepts requests for 1.0 or 1.5 when rebuilding old source.
Cross-minor binary substitution is not promised.

Integer machine-code layouts and private compilation helpers changed. Do not
rely on private native entries or generated instruction offsets. Batch samples
still time the same records and omit unsampled execution from sampled totals.
The first batch on a new handle remains interpreted. Code still uses separate
RW-to-RX mappings, so smaller text does not remove page-rounding overhead.
No new CPU extension is required. See [performance-1.6.md](performance-1.6.md)
for measured trade-offs and [release verification](release-verification-1.6.md)
for source compatibility and package checks.

## Earlier migration: v1.0 to v1.5

Rebuild existing source with matching 1.5 headers and library. The SONAME changes
from librecode.so.1.0 to librecode.so.1.5; no cross-minor binary compatibility is
claimed. RECODE_API_VERSION remains 1. Public version and ABI component macros
are generated as 1.5.0 and 1.5. Existing function signatures, enum values and
configuration fields remain available, with their scalar semantics intact.
Same-major CMake requests, including find_package(Recode 1.0 CONFIG REQUIRED),
continue to support rebuilding old sources. New batch consumers should request 1.5.

Batching is additive and optional. Replace a scalar loop with an item array and
rc_runtime_call_integer_batch when the amortized dispatch cost justifies the
array validation and request setup. Keep every descriptor/argument/view readable
until return. Require disjoint results/completed/error storage. Check completed
on failure: earlier outputs are valid, the failing and later slots are unchanged,
and error.offset is still an instruction index. See [batch execution](batch-execution.md).

First use of a new handle interprets its entire batch. Later boundaries can
compile; this differs from a scalar loop's per-call policy opportunities. Invocation
and tier counts count items, cache acquisition counts count requests. Sampled
execution totals do not include unsampled items. Long batches pin native code and
can postpone cache eviction/removal. Existing BUSY and retryable cleanup rules
remain. No native pointer or cancellation interface is introduced.

Generated integer bytes are smaller and may run faster on the measured fixtures,
but instruction layout is private and some compilation/managed timings regress.
Default adaptive thresholds and executable page allocation are unchanged. Benchmark
your workload; see the [batch study](batch-verification.md) and
[compiler study](../benchmarks/competitors/runtime-optimization.md).

## Earlier migration: v0.7 to v1.0

Rebuild consumers against matching 1.0 headers and library. The new SONAME is
librecode.so.1.0; the old experimental librecode.so.0 binary ABI is not retained.
There are no intentional source-breaking changes to supported v0.7 function
signatures, existing enum values or struct fields. CMake source compatibility
accepts the same major release; binary compatibility covers only patches within
one major/minor. See public-api.md for the precise policy.

Additive changes are `struct rc_memory_view`, `RC_INT_LOAD_U8/U16/U32/U64`,
`RC_BOUNDS`, `rc_integer_interpret_view` and `rc_runtime_call_integer_view`.
Existing calls supply an empty view. Existing memory-free programs behave as
before. Update status/opcode switches with a default error path for future values.
Integer arguments remain numbers, never pointers. Buffers are borrowed only for
the duration of an explicit view call and must not be mutated concurrently.

To read data, append a LOAD with dst and left (byte offset register); zero all
unused fields. Keep each actual buffer alive/readable for its declared length,
check the returned status, and consume result only on OK. Bounds checks are
unsigned and overflow-safe, but arithmetic producing offsets retains ordinary
modulo-2^64 semantics. See examples/data-rules for a complete migration pattern.

Private src headers and helper symbols are unsupported. Shared builds now hide
those symbols. Replace private emitter calls with public program registration
and managed calls; do not depend on internal native ABIs. Use the default-config
function and then override named settings. Continue to honor removal BUSY,
retryable release failure and caller-excluded destruction contracts.

Performance is not unchanged: the expanded interpreter dispatch regressed in
v0.8 and native compilation now revalidates internal inputs. Existing native
memory-free instruction layouts remain the same. Benchmark your actual workload;
see application-verification.md and release-verification.md for measured costs.
