# Integer programs

Include `<recode_integer.h>` to construct programs without machine-code bytes.
The existing accumulator decoder, IR, low-level APIs and TRACE policy are unchanged.
Integer programs use mutable numbered 64-bit registers with explicit operands,
comparison results and branch conditions. There are no implicit flags or implicit memory
operands. Explicit bounded loads use a [call-scoped memory view](memory-views.md). This is a bounded execution model, not a general C compiler.

Create a builder with 1..64 registers and 0..2 arguments. Append instructions in
execution order, then finalize into an immutable owned program. Branch targets
are absolute zero-based instruction indexes; forward targets are allowed until
finalization. Every unused field must be zero. Append rejects invalid operations,
registers, argument indexes and targets beyond the 4,096-instruction bound.
Failures do not append a partial instruction. Finalization rejects an empty
program, out-of-range targets, escaping fallthrough, unreachable instructions and
reads undefined on any incoming path, including entry edges through cycles.
The must analysis intersects definitions over reachable predecessors to a fixed
point. Its bounded sweeps can be quadratic on adversarial layouts.

`rc_builder_finalize` copies the program and keeps the builder editable and owned
by the caller. Both pointer outputs must initially be NULL. Destroy builders with
`rc_builder_destroy` and programs with `rc_integer_program_destroy`; NULL is valid.
Do not copy ownership or destroy objects during use. Builders are not synchronized;
immutable programs permit concurrent readers. As with other C APIs, forged/stale
opaque pointers and overlapping output storage are caller misuse.

| Operation | Used fields | Semantics |
| --- | --- | --- |
| ARGUMENT | dst, immediate | Copy input 0 or 1 into dst |
| CONSTANT | dst, immediate | Full uint64_t bit pattern |
| ADD, SUB, MUL | dst, left, right | Unsigned arithmetic modulo 2^64 |
| AND, OR, XOR | dst, left, right | Bitwise operations |
| SHL, SHR | dst, left, right | Logical shift by right & 63, including counts >=64 |
| EQ, NE | dst, left, right | Equality/inequality, result exactly 0 or 1 |
| ULT, ULE | dst, left, right | Unsigned less-than/less-or-equal, 0 or 1 |
| SLT, SLE | dst, left, right | Two's-complement signed comparison of bit patterns, 0 or 1 |
| JUMP | target | Unconditional branch |
| BRANCH | left, target | Branch if left != 0; otherwise next instruction |
| RETURN | left | Return that register's uint64_t value |

Read operands are captured before dst is written, so dst may alias either source.
A register defined on only one arm is undefined at the join. Loops may overwrite
registers but cannot obtain their first definition solely from a backedge.
Closed loops are allowed and may not terminate. There are no division, arithmetic
right shift, external calls, pointer operands or implicit conversions.

For example, this computes `a + 2*b`:

```c
struct rc_integer_insn insns[] = {
    {.op=RC_INT_ARGUMENT, .dst=0},
    {.op=RC_INT_ARGUMENT, .dst=1, .immediate=1},
    {.op=RC_INT_CONSTANT, .dst=2, .immediate=2},
    {.op=RC_INT_MUL, .dst=2, .left=1, .right=2},
    {.op=RC_INT_ADD, .dst=2, .left=0, .right=2},
    {.op=RC_INT_RETURN, .left=2}
};
```

The independent [rules application](../examples/rules/main.c) supplies a complete
checked build/finalize/register/execute/remove/destroy lifecycle and three rules,
including `a > b ? a + 2*b : a ^ b`. It compares results with separate C expressions
under INTERPRET, FIXED and ADAPTIVE, using boundary input distributions.

```sh
cmake -S examples/rules -B build-rules -DCMAKE_PREFIX_PATH=/your/installed/prefix
cmake --build build-rules --parallel 2
./build-rules/rules
```

`rc_integer_interpret(program, arguments, count, budget, &result, &error)` directly
executes an immutable finalized program. Count must exactly match its declaration;
NULL arguments are accepted only for zero inputs. A positive budget counts every
instruction including RETURN; exhaustion returns RC_BUDGET without changing result.
Zero means unlimited. Registration with `rc_runtime_register_integer` copies the
program, so it can then be destroyed. `rc_runtime_call_integer` uses the same
argument-count contract, and accepts only integer handles. `rc_runtime_call`
accepts only accumulator handles. Wrong representation/count returns RC_ARGUMENT
before changing invocation counts or result.

INTERPRET never compiles; FIXED and ADAPTIVE use whole-function native compilation
with the same thresholds, sampling, failure fallback and cooldowns as accumulator
functions. TRACE registration returns RC_UNSUPPORTED and leaves the handle
unchanged: this version does not trace virtual-register state. No fallback is
silently advertised as tracing. Compilation failures remain nonfatal to execution
and are visible in per-function stats. `optimize` applies only to the accumulator
optimizer; integer lowering is the same with either setting.

Integer and accumulator translations share the runtime's lookup, LRU, lock,
active-handle ownership and executable-memory ledger. Identity distinguishes the
representation, register count, input count and every instruction field; equality
never compares padding. Aliases share immutable IR and native code. Integer
program storage contributes to program_bytes, including its private descriptor.
No executable pointer is exposed. Removal, invalidation, clear and destruction
retain the documented BUSY and failed-release retry contracts.

Native lowering uses RAX/RCX scratch registers and stack slots, preserving RDI/RSI
inputs and all callee-saved registers. Every function starts with ENDBR64, allocates
at most 512 stack bytes, makes no calls and restores RSP on every return. Slots are
8-byte aligned. The entry stack is 8 mod 16 and a 16-byte-multiple frame preserves
that; there are no calls needing a new alignment adjustment. v0.7 lays out exact instruction sizes using a bounded offset table,
removing v0.6's fixed-slot NOP padding. Emission uses 32,776 bytes of bounded temporary
offset storage on the C compiler's stack; generated functions still need at most
512 bytes. All branches are rel32 within the bounded function. Compilation reserves page-rounded budget, emits RW, then protects RX
before publishing a private System V AMD64 entry. Memory-free programs retain
the two-argument entry; memory programs additionally receive the view and a
private status/result channel. Failed cleanup
retains the mapping with no callable entry. No sophisticated register allocator
or performance claim is part of v0.6.

The v1.0 [public API policy](public-api.md) defines compatibility for these
interfaces. Platform, lifetime, security and ABI limits from [compatibility.md](compatibility.md)
and [embedding.md](embedding.md) still apply. Programs are not a sandbox and the
managed runtime has no execution deadline. Sanitizers do not instrument emitted
instructions. Program/metadata memory has no runtime-wide quota.
