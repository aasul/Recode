# Recode v1.7.0 release preparation

This candidate contains four runtime changes for Linux x86-64 C11 applications:
fused native batch loops, opt-in cold compilation, immutable shared executable
pages and direct fixed-stride record execution. It is a review candidate; no
merge, tag or GitHub release is authorized by this preparation.

All existing public functions and enum values remain. API version stays 1;
minor ABI and SONAME become 1.7. Rebuild existing consumers with matching headers
and library. Binary compatibility across minor releases is not claimed.

The default first batch still interprets. Set cold_batch_compile to request
cost-based early compilation or alias reuse. Failed compilation falls back to
interpretation. The record API supports 0..2 unaligned uint64_t arguments and a
bounded little-endian memory view with ordered prefix failures. The existing
item-array API retains its contract.

Shared integer pages are immutable after publication. Replacement needs a spare
page and no source-page pins. Removed entries cannot unmap active or shared code;
failed cleanup mappings remain owned and charged. Large, pinned and constrained
cases retain isolated allocation. Compilation, metadata and sampled-entry costs
remain trade-offs; this is not a total process-memory quota.

See [native entry details](native-batch.md), [cold policy](cold-batch.md),
[slab invariants](code-slabs.md), [record API](contiguous-records.md),
[migration](migration.md), [performance evidence](performance-1.7.md) and
[final verification](release-verification-1.7.md).
Native instructions are outside sanitizer instrumentation. No arbitrary-binary,
new CPU-extension, execution deadline or in-process sandbox support is added.
