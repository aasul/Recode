# Restricted translator contract

The input is one straight-line System V AMD64 function. RDI contains the argument,
RAX contains the returned value, and RET uses the caller's return address. The
accepted instructions touch no memory except the return stack access and modify
no callee-saved registers. No flags or intermediate register values are observable
within this instruction subset. Equivalence is limited to the return value,
not full machine state or exception behavior.

The supported encodings and immediate behavior follow Intel's instruction set
reference for MOV, ADD, SUB, NOP, and RET:
[Intel SDM volumes 2A through 2D](https://www.intel.com/content/www/us/en/content-details/835757/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-2a-2b-2c-and-2d-instruction-set-reference-a-z.html).
GNU as/objdump also independently confirmed the tested MOV, accumulator imm32,
and register imm8 encodings during implementation.

## IR and arithmetic

Each IR instruction has an operation and raw 32-bit immediate. CONST zero-extends;
ADD/SUB sign-extend. No instruction contains an input-byte pointer or depends on
its original encoding. The decoder reports byte offsets, while IR validation
reports instruction indexes. For trailing input, decoding identifies the RET that
terminated the function. Empty input and a complete stream without RET report a
missing return; an incomplete supported encoding reports truncation. All errors
include a status, offset, and system errno where applicable.

The optimizer validates the whole IR before modifying it. It sums consecutive
arithmetic, allowing intervening NOPs, in `uint64_t` modulo 2^64. It never combines
across a register assignment. The sum must fit a sign-extended imm32, or its
modular negation must fit a SUB imm32. For example, adding INT32_MAX then 1 can
become SUB with immediate bits 0x80000000; adding INT32_MAX twice remains two
operations. Unsigned subtraction avoids negating INT32_MIN in signed arithmetic.
Compaction is in place and never increases instruction count.

## Ownership and execution

`rc_decode` and `rc_compile` take zero-initialized empty output objects. A successful
decode owns one allocation, released with `rc_program_free`. Callers can also
supply a borrowed IR array to validation, optimization, and emission, but must not
pass that borrowed array to `rc_program_free`. Counts must describe valid array
storage. Buffers and output objects must not overlap. The byte input must remain
readable and unchanged throughout decoding; no input bytes are executed directly.

Emission first validates and calculates the required size. A size query allocates
nothing. An undersized output is untouched and reports the required length.
Generated code uses canonical accumulator encodings and an ENDBR64 entry marker.
It may be larger than input, especially when canonicalizing imm8 instructions or
adding the entry marker. Optimization is not a promise of smaller machine code.

Compilation allocates a private anonymous RW mapping, emits into it, then changes
it to RX with `mprotect`. There is no RWX mapping or writable alias. Linux rounds
mapping lengths to page boundaries. Linux x86-64 provides coherent instruction
and data caches for this use. The function-pointer representation is an explicit
Linux x86-64 ABI dependency; it is copied with `memcpy` rather than an ISO C cast.

A compiled object owns its mapping until `rc_code_free` succeeds. A failed release
retains ownership so it can be retried. Do not copy owning objects, call a function
after release, or release while calls are in flight. Concurrent calls to an
unchanging compiled function use no shared mutable runtime state. Mutation,
compilation output, and release require caller synchronization. There is no global
cache, instrumentation, signal handler, or process loader.
