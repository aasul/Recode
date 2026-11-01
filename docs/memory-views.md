# Bounded application data

v0.8 adds `rc_integer_interpret_view` and `rc_runtime_call_integer_view` to
`recode_integer.h`. Both accept the existing 0..2 integer arguments and an optional
`const struct rc_memory_view *`. The view contains `const void *data` and
`size_t length`. NULL view means empty; NULL data is valid only for length zero.
Nonzero length requires an actual readable object of at least that many bytes.
The library cannot verify the validity of a caller's pointer or declared length.

The caller owns the descriptor and bytes, keeps them valid throughout the call,
and excludes simultaneous writes or destruction. Different calls can use different
buffers concurrently, including on the same handle. Immutable shared buffers are
also allowed. Recode copies no buffer, retains no view and embeds no caller pointer
in executable code. Buffers can be reused or freed immediately after return.
Descriptor, input, output and error storage must obey the C API's nonoverlap rules.

`RC_INT_LOAD_U8/U16/U32/U64` use `dst` and `left`; all other fields must be zero.
The value of register `left` is an unsigned byte offset from the start of the view.
A load zero-extends 1, 2, 4 or 8 little-endian bytes into `dst`. Source/destination
aliasing is permitted. Alignment is unrestricted. C interpretation assembles bytes;
x86 native code uses unaligned loads. Neither path accesses bytes beyond that width.
The load first checks `offset <= length` then `width <= length - offset`.
There is no unchecked offset-plus-width calculation. Integer arithmetic used to
produce offsets still wraps modulo 2^64, as documented for all integer programs.

A failed load immediately returns `RC_BOUNDS`, sets `error.offset` to the zero-based
instruction index and `system_errno` to zero, and leaves the caller's result
unchanged. Earlier virtual-register calculations have no external effects.
Unused/skipped loads cannot fail. Invalid descriptors return `RC_ARGUMENT` before
execution. Optional errors may be NULL. The original execution functions supply an
empty view; they can execute a memory program if no load is reached.

Registration, identity, cache budgeting, publication, pins, eviction and cleanup
follow the existing runtime contract. Identity includes each load operation and
all instruction fields but never the call's buffer or bytes. Compilation failure
still falls back to interpretation, which can itself return a bounds error.
Interpreted/jitted statistics count attempted executions, including bounds failures;
invalid API arguments do not count as executions. There are no stores, raw pointer
operands, external calls or integer traces. TRACE registration rejects these programs.
A memory view does not make Recode a sandbox or impose a native execution deadline.

The independent [data-rules consumer](../examples/data-rules/main.c) reads enabled,
quantity, price and credit fields from a 15-byte record. Its arithmetic and branches
run inside Recode. It compares an independent C calculation with direct interpretation
and managed INTERPRET/FIXED/ADAPTIVE calls, including truncated records and misalignment.
Build against an installed library as described in [embedding.md](embedding.md):

```sh
cmake -S examples/data-rules -B build-data-rules -DCMAKE_PREFIX_PATH=/your/prefix
cmake --build build-data-rules --parallel 2
./build-data-rules/data-rules
./build-data-rules/data-rules --bench
```

CTest builds and runs this application against relocated static and shared installs.
`memory` tests every width at guarded boundaries, offsets through UINT64_MAX,
unaligned data, 12,000 deterministic input comparisons, branch/backedge behavior,
ABI sentinels, call-scoped buffers and 12,000 concurrent calls racing cache pressure.
`adaptive_failures` injects mmap, mprotect and munmap failures for memory programs,
checks interpretation and bounds errors after failed compilation, and retries cleanup.
The existing allocation sweeps and accumulator/integer regression tests remain enabled.
Generated instructions are not sanitizer-instrumented; guard pages test actual loads.
