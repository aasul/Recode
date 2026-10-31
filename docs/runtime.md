# Adaptive runtime

Recode v0.3 adds an owned runtime above the restricted translator. The accepted
encodings and return-value/branch contract in [translator.md](translator.md)
continue to apply. It does not load ELF files or instrument existing processes.
The low-level `rc_compile` and `rc_code_free` API remains available with its
original caller-managed lifetime.

## Registration and calls

Initialize configuration with `rc_runtime_default_config`, change the desired
fields, then pass it to `rc_runtime_create`. The output pointer must initially
be NULL. Configuration is copied and cannot change during the runtime's lifetime.
Zero executable budget is valid and prevents compilation from creating mappings.
Zero thresholds, retry intervals, sampling periods, reuse horizons, expected
reuse, or initial compilation estimates are rejected.

```c
struct rc_runtime_config config = rc_runtime_default_config();
config.executable_budget = 4 * 1024 * 1024;
struct rc_runtime *runtime = NULL;
struct rc_error error;
enum rc_status status = rc_runtime_create(&config, &runtime, &error);
/* Check status before using runtime. */
const uint8_t input[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,7,0xc3};
rc_handle handle;
status = rc_runtime_register(runtime, input, sizeof(input), &handle, &error);
/* Check status; input can then be changed or released. */
uint64_t result;
status = rc_runtime_call(runtime, handle, 35, &result, &error);
/* On success result is 42. Repeated calls collect evidence for compilation. */
```

All public calls return `rc_status`; optional error objects carry status, offset,
and system errno. Supply valid, nonoverlapping argument/output storage. Failed
registration and execution leave the result output unchanged. There are no hidden
global runtime objects. A nonzero numeric handle belongs to its runtime only;
passing a handle to a different runtime is caller misuse. IDs are never reused
within a runtime. Exhausting the ID space rejects registration with RC_CAPACITY.
Removed or unknown IDs return RC_NOT_FOUND. Never copy ownership of a runtime.

Registration decodes and validates before publication, then owns a compact copy
of the decoded IR. The input buffer is borrowed only during registration and must
stay readable and unchanged for that call. Identical operation/immediate arrays
share a translation, including alternate encodings that decode identically.
Comparison examines all fields, not a hash, source address, or struct padding.
Optimization mode and compiler assumptions are immutable properties of the owning
runtime; translations never cross runtime boundaries. Each registration owns a
separate handle and profile even when its translation is shared.

Every handle starts by interpreting. The interpreter implements INPUT, CONST,
ADD, SUB, CMP, TEST, JMP, JZ, JNZ, NOP and RET itself. It tracks RAX and ZF,
zero-extends CONST, sign-extends arithmetic/comparison immediates, uses unsigned
wrapping arithmetic, and takes absolute IR targets. MOV preserves ZF. Runtime
calls use already validated immutable IR without revalidating each invocation.
The public `rc_interpret` validates caller-supplied IR on each call. A zero
instruction budget means unlimited execution; positive budgets count instructions,
including RET, and return RC_BUDGET without changing output if exhausted.
Runtime calls have no instruction budget or termination guarantee.

## Profiling and tier selection

`rc_runtime_function_stats` copies a consistent snapshot under the runtime mutex.
Invocations count accepted calls on entry. Interpreted and jitted counts record
completed calls. During execution the sum can therefore lag invocations.
Attempts include preparation, optimization, size analysis and capacity failures;
successes count installed translations, not every alias that reuses one.
`last_compile_error` retains the last nonfatal compilation failure until that
handle successfully compiles. An alias cache hit does not clear it.

Cache hits mean a handle found existing native code after its initial interpreted
call; misses include first calls, policy declines, and calls that compile.
A successful compilation on a miss is not also counted as a hit.
Generated size is the last successful native size seen by the handle, not its
current mapped size. Cache-wide mapped usage comes from `rc_runtime_get_stats`.

Counters and cumulative nanoseconds saturate at UINT64_MAX. Size/reference/pin
limits reject operations rather than wrap. Timing uses CLOCK_MONOTONIC around
execution outside the mutex, so it includes scheduling delays but excludes
lookup, lock wait, and compilation. The first eight completed samples in each
tier and every `sample_period`th invocation are timed. Concurrency can produce
more than eight initial samples. `interpreted_ns` and `compiled_ns` are sums of
sampled durations, not estimates of total execution time. Sample counters give
the denominator. Compilation time measures the whole compilation attempt inside
the mutex. Timer failures or zero durations increment `timing_failures`; timing
failure does not change the result. Benchmarks independently measure total costs.

Four policies are available:

- RC_POLICY_INTERPRET always interprets, including aliases of cached functions.
  This policy applies to the whole runtime, so it cannot contain native entries.
- RC_POLICY_FIXED considers compilation when the invocation count reaches
  `hot_threshold`, provided an interpreted call has completed. With threshold 32,
  a sequential caller interprets 31 calls and compiles on call 32. Threshold 1
  still requires the first completed interpreted call.
- RC_POLICY_ADAPTIVE adds measured cost and reuse checks to that threshold.
- RC_POLICY_TRACE runs the interpreter with hot-loop recording and native trace
  entry, with no whole-function compilation. See [tracing.md](tracing.md).

The adaptive policy requires eight interpreted timing samples. It maintains an
integer moving average with weight approximately 1/8 and caps individual policy
samples at 1 ms, retaining uncapped sampled totals. Before a native measurement
exists, it uses `initial_native_ns` (default 50 ns). Compilation cost begins at
`initial_compile_ns` (50 us), then uses a runtime-wide moving average of successful
attempt costs. Those costs are not clipped, because expensive compilation must
remain visible to the decision.

Estimated future calls are bounded by `expected_reuse` (4096) and the observed
invocation rate over a `horizon_ns` window (100 ms). Activity windows reset after
that horizon on a cache miss; a long idle interval first lowers the prediction.
Compilation requires positive estimated savings and predicted reuse greater than
twice `compilation_cost / savings_per_call`. It also requires room for at least
one page or an unpinned mapping that could be evicted. Exact size and capacity are
checked after preparation. These are coarse heuristics, not reliable workload
forecasts. Call frequency includes the caller's time between calls.

After compilation, native timings revise the cost estimate. Retirement requires
eight consecutive sampled calls slower than interpretation by over 25%, a moving
average above the same margin, and no native pins. It releases the translation
and imposes a cooldown. Occasional timer/scheduling outliers alone should not
retire code. Retirement affects aliases sharing that translation; their inputs
may have different costs. `policy_postponed` counts failed adaptive profitability
checks and retirement decisions, not threshold warmup or cooldown calls.

Nonfatal compilation, capacity, or protection errors execute the current call in
the interpreter and remain visible in statistics. A shared translation cooldown
prevents aliases from retrying immediately. Failure delays start at `retry_calls`
(128 accepted calls to that translation) and double up to 512 times that interval,
using saturating arithmetic. Aliases share the counter; unrelated functions cannot
advance a cooldown. After counter exhaustion, a saturated cooldown remains disabled.
Repeated native profitability retirement uses the base cooldown; fresh interpreted
evidence must still pass the cost check before recompilation.

## Cache ownership and budgeting

One translation owns its immutable IR, optional mapping, reference count and
execution pins. Entries are ordered by native use, most recently used first.
Insertion and native lookup move an entry to the front. Eviction scans for the
least recently used unpinned mapping. Translation records and IR remain while
registered handles reference them. There is no cache of removed functions.

The executable budget is a strict bound on the virtual bytes in all runtime-owned
code mappings, rounded up to the host page size. It covers RX code and RW mappings
retained after failed compilation cleanup. It is not an emitted-byte, resident
memory, or total-process-memory bound. The code size query precedes mmap. Capacity
is made available under the mutex before allocation; the runtime never temporarily
maps beyond the budget. Mapping starts RW, becomes RX only after successful
emission, and has no RWX interval or writable alias. No native entry is published
until protection succeeds.

If capacity is insufficient, the runtime releases eligible LRU mappings and retries
space accounting. Oversized code or all-pinned capacity yields nonfatal RC_CAPACITY
in compilation statistics. Unmapping failure disables that native entry, retains
ownership and accounting, and prevents reuse of the unreclaimed space. Cleanup,
removal or a later compilation attempt can retry. Both generation and rollback
failure retain the mapping with a NULL native function. Runtime `release_failures`
and `last_cache_error` include failed rollback and deferred retirement cleanup.
No mapping pointer or native function pointer escapes through runtime APIs.

Persistent requested IR bytes and runtime/handle/translation metadata bytes are
reported separately. Metadata includes the mutex and profile fields but excludes
allocator headers. Temporary decoder arrays, CFGs, compilation copies, thread
stacks, library state, and caller buffers are outside these counters and outside
the executable budget. Registrations can consume unbounded data memory if callers
keep registering new functions. A total data-memory quota is not implemented.

## Concurrency and cleanup

One pthread mutex protects handles, cache transitions, pins, policy and profiles.
Compilation is serialized under it, avoiding duplicate compilation, budget
reservation races and partially published entries. This can delay other API
operations during compilation. Native execution and interpretation both occur
outside the mutex. A handle's active-call count keeps its IR/profile alive, and
a native entry's pin count keeps its mapping alive until execution finishes.
Pin acquisition and lookup are atomic with respect to eviction under the mutex;
the function result is saved before releasing the pin.

Operations on live runtimes can execute concurrently except destruction:

| Operation | Ownership and result |
| --- | --- |
| `rc_runtime_remove` | RC_BUSY leaves an active handle registered. Otherwise remove that handle; last reference also releases mapping and IR. Release failure retains the registration for retry. |
| `rc_runtime_invalidate` | Affect all aliases of the handle's translation. RC_BUSY leaves pinned code untouched. Successful invalidation imposes a cooldown. Release failure disables native lookup and retains the mapping. |
| `rc_runtime_clear` | Try every unpinned mapping, retaining IR/handles/profiles. RC_BUSY means some mappings remain pinned; a release error takes precedence. Other entries may already be cleared. Future calls may compile again. |
| `rc_runtime_destroy` | Caller must exclude new API entry and finish concurrent registration, removal, queries and cache operations. Already accepted calls may finish; nonzero active calls return RC_BUSY. After successful release, free all registrations, IR, mappings and mutex, then set the pointer to NULL. Release failure retains the runtime and profiles for retry; some mappings may already have been cleared. |

A runtime with no handles retains only its base runtime object and mutex; all IR,
cache records, and mappings have been released. Destroy releases that base object.
Join caller threads before final destruction. The runtime cannot protect a caller
that starts using a freed runtime pointer, or detect threads waiting to enter an
API before they acquire the mutex. It is not signal-safe, reentrant from signals,
pthread-cancellation-safe, or usable after fork from a multithreaded process before
exec. Separate runtimes do not coordinate. Low-level code objects still require
the caller to exclude release while executing.

## Current trade-offs

Handle and translation identity lookup use growing hash buckets with full IR
comparison on hash matches. MRU updates use a doubly linked list. Eviction still
scans ineligible entries, and collisions can degrade lookup. Buckets and trace
metadata count toward reported metadata usage. Mutexes, clocks, statistics,
validation and compilation add real overhead; direct native calls remain much cheaper for tiny functions. Adaptive
estimates are sensitive to CPU frequency, scheduling, timer cost, changing inputs,
and callers' future behavior. See [runtime-verification.md](runtime-verification.md)
for measured regressions, failure tests and sanitizer coverage.
