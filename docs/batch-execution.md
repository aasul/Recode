# Batched integer execution

`rc_runtime_call_integer_batch` evaluates one registered integer program over
independent items in order. It supports INTERPRET, FIXED and ADAPTIVE, with the
same memory-free and bounded-load instruction semantics as scalar calls. Integer
registration with TRACE remains unsupported. Programs still have 0..2 arguments.

```c
struct rc_integer_batch_item items[] = {
    {arguments_a, 2, &view_a},
    {arguments_b, 2, &view_b}
};
uint64_t results[2];
size_t completed;
struct rc_error error;
enum rc_status status = rc_runtime_call_integer_batch(
    runtime, handle, items, 2, results, 2, &completed, &error);
```

The [installed example](../examples/batch-rules/main.c) contains a complete
compilable application: an enabled record's unsigned score must exceed a supplied
threshold, returning the excess or zero. It compares an independent little-endian
C calculation, handles bounds errors, queries statistics and releases resources.
Its independent CMake project uses only public headers and `Recode::recode`.
CTest builds/runs it against relocated static and shared installations.

All input descriptors, argument arrays and readable view bytes remain borrowed
until return. No pointers are retained. Read-only inputs may overlap one another.
Results, completed and optional error storage must be disjoint from each other
and all input ranges. Overlapping output is rejected with RC_ARGUMENT without
writing any output, including completed/error. Only the item_count result slots
are written or checked for overlap; unused result capacity is outside that range.
The library checks size/address wrap and visible range overlap, not whether a
pointer really refers to a readable/writable object of the claimed length.

completed is mandatory. Ordinary structural errors zero it and execute nothing:
wrong arity, NULL required arrays, malformed view, address/count overflow or a
non-integer handle. Insufficient capacity returns RC_CAPACITY; invalid pointers
or counts return RC_ARGUMENT; unknown handles return RC_NOT_FOUND. For zero items,
NULL items/results are accepted, the handle/representation is checked, completed
is zero, no invocation is counted and no code is compiled. Alias rejection is the
exception to clearing completed. error may be NULL. The caller must supply valid
storage even when deliberately testing malformed requests.

Every item is structurally validated before the first evaluation. Executed loads
can still fail. Execution stops at the first failure: successful earlier slots
remain valid; the failed slot and every later slot stay unchanged. completed is
the number of successes and therefore identifies the failed item. error.offset
continues to identify an instruction, never a record. A batch is not atomic.
Integer arithmetic wraps modulo 2^64 and skipped loads do not fail.

The runtime looks up the handle once, protects its lifetime, selects a tier once,
and processes all records without holding the cache mutex. A native entry is
snapshotted and pinned until all its calls finish. Concurrent scalar/batch callers,
registration and cache pressure are supported. Removing an active handle returns
BUSY. Invalidation/clear cannot release pinned code; a long batch can delay eviction
and cause another compilation to fall back when no unpinned capacity remains.
Join callers and exclude new API entry before destruction. Cancellation, signal
handlers, execution timeouts and hostile in-process sandboxing remain unsupported.

Tier decisions happen at batch boundaries. By default every new handle interprets its entire
first batch, including when an alias has cached code. The additive
[cold_batch_compile option](cold-batch.md) permits earlier native execution. FIXED/ADAPTIVE can compile
on a later request based on completed evaluation history. Compilation failures
fall back for the entire batch and retain existing retry cooldowns and cleanup
ownership. Batches do not force adaptive promotion or change scalar thresholds.
A single huge first batch can therefore be slower than scalar calls that promote
partway through. Use scalar calls for isolated evaluations and bounded batches
for repeated record processing; measure the cold and warm paths separately.

Invocation/interpreted/jitted counters count attempted item evaluations, including
a failing bounds check. Invalid structural requests and zero batches count none.
Cache hit/miss counters count actual tier acquisitions, once per batch request.
active_calls counts public requests in flight, rather than their item count.
The first invocation is reserved before execution; remaining attempts and tier
statistics are merged at completion under the mutex. In-flight snapshots need not
yet include all items. Saturating counters retain the scalar overflow behavior.

The first eight samples in a tier and periodic item samples still read the clock;
execution totals and sample counts cover only those attempted items. Batch policy
cost estimates merge their mean once at completion, while scalar calls retain
per-call updates. Slow-native feedback is considered once per sampled batch.
Concurrent completion and inputs can therefore change adaptive decisions. No
batch timing or promotion pattern is a portability or profitability guarantee.

Run the independent example's checked scalar/batch study with `batch-rules --bench`.
It alternates scalar/batch order over three rounds, tests sizes 1, 8, 32, 128,
1,024, 4,096 and 65,536, and retains cold execution/total and warm ns per record
with policy/tier counters. Input preparation and reference checks are outside the
timed region for both paths; both write identical result arrays. Warmup executes
at least eight requests and 128 records; timing covers at least eight requests
and approximately 32,768 records. Large batches include more records by design.
The cold first batch may remain interpreted while scalar calls compile. The
static dispatch probe separately verifies lock/clock/allocation counts. Generated
native instructions remain outside sanitizer instrumentation.
