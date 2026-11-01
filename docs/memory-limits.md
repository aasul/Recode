# Memory and execution limits

The executable budget bounds all page-rounded live native mappings, including
failed rollback/release mappings. Active pins can prevent eviction, causing a
nonfatal compilation-capacity failure and interpretation. Program_bytes counts
requested owned IR and integer descriptors. Metadata_bytes counts the runtime,
lookup buckets, handles, translations, trace slots and owned trace operations.
Neither counter includes allocator overhead, C stack, temporary validation/emission
allocations, thread stacks or RSS. Arithmetic checks prevent counter overflow.

A distinct persistent-memory quota was evaluated for v0.9 and deferred. The current
ledger is useful for observation but does not bound temporary decoder/CFG memory
or allocator overhead. A new quota would require an explicit reservation/lifetime
contract for index replacement and trace recording, and still would not be a
process memory limit. The API freeze does not present the executable budget as
a total allocation quota. Default behavior remains compatible.

Embedders should cap source-byte length before decoding/registration, cap the
number of registrations and distinct translations, bound integer program size,
remove unused handles, and bound runtime lifetime. Integer limits are 64 registers
and 4096 instructions; byte decoding can accept much larger input and allocate
proportionally. Adversarial dataflow layouts can require quadratic validation
work. Apply application input limits even when executable_budget is zero.
A process/container resource limit is required for a firm total-memory boundary.

Finalization validates control flow, not termination. Managed calls and native
functions have no execution timeout. Direct interpreters accept instruction
budgets; a process supervisor can terminate a worker containing native execution.
Do not attempt to cancel a thread inside the runtime: locks and pins would remain
owned. Join all callers and stop new entry before runtime destruction.
