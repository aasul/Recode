# Migrating v1.0 to v1.5

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
