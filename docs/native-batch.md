# Private native batch entry

Managed integer translations contain an unchanged scalar entry and an aligned
batch entry. Both are emitted from the same register allocation and instruction
lowering. Every validated integer operation is supported, including loads and
backedges. The additional body increases compilation work and native text size.
The low-level diagnostic scalar emitter remains scalar-only.

The batch entry has the private signature `rc_status(context *)`. The context
contains input/result pointers, a count, a successful prefix, a fault instruction
index, an optional record layout and a stride. Only validated requests reach it.
RDI receives the context. Saved R12 holds it, R13 the current item, R14 the output
cursor and R15 the remaining count. RBX holds an optional record layout and RBP
the input stride. The allocator uses caller-saved registers only.
RBX/RBP/R12..R15 are pushed once and restored
on every exit. Vector and floating-point state are untouched. RSP is
restored exactly. The leaf makes no calls and needs no aligned vector spills.
The entry RSP residue of 8 modulo 16 remains unchanged by its six pushes and
16-byte spill frame. The convention follows the
[System V AMD64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex).

A RETURN branches to a common result store and native loop backedge. An executed
failed bounds check branches to a common fault exit without storing that result
or advancing its cursor. Completion is count minus remaining; the error index
comes from the same R10 instruction marker as scalar memory entries. NULL views
become empty views. A skipped LOAD remains skipped. No future record is loaded
by the architectural instruction stream before the current record succeeds.

All branches are intra-translation rel32 displacements. There are no absolute
addresses, external calls or RIP-relative data references. ENDBR64 marks each
indirect entry. Count/layout limits bound code well below rel32 range.

The runtime calls this entry once for each consecutive unsampled run. Required
timed items still use the scalar entry, preserving the first eight tier samples,
periodic sample positions, attempted-item counters and exact error ordering.
For a steady 128-item batch at period 16, eight sampled scalar entries plus eight
fused entries replace 128 scalar entries. Period 1 intentionally remains scalar.
The first batch remains interpreted, including aliases. Tier selection, cooldowns
and interpreter fallback are unchanged.

Scalar and batch entries belong to one RW-then-RX mapping. Publication is under
the runtime mutex; one translation pin covers the entire batch after lookup.
Execution occurs outside that mutex. Invalidation, eviction and destruction keep
the existing BUSY/retry behavior. More text can require an additional page for
large translations; executable-budget fallback still interprets correctly.

Tests directly invoke the fused entry through assembly ABI sentinels, compare
independent C arithmetic and randomized results, exercise zero counts, maximum
IR size, spills, backedges and every load width at guard-page boundaries. Public
batch suites check sampling, partial faults, untouched suffixes, aliases and
concurrent cache pressure. Sanitizers instrument C, not generated instructions.
