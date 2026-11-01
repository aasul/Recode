# Migrating v0.7 to v1.0

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
