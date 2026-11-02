# Contiguous record batches

`rc_runtime_call_integer_records` adds direct traversal of fixed-stride records.
The descriptor-array API remains available with its existing contract.

Construct an immutable `rc_integer_record_batch` with a record base/count,
stride, declared record_size, argument_count and up to two byte offsets for
uint64_t arguments, plus a view_offset/view_length. Pass a separate result array,
capacity and mandatory completed output. See examples/contiguous-rules for a
standalone installed-package consumer with independent C results and errors.

stride must be at least record_size. Each argument must fit eight bytes inside
record_size, regardless of alignment. Argument object representations are loaded
with memcpy in C and unaligned MOV in x86-64 code. The memory view must fit within
the record; its integer LOAD operations remain unsigned little-endian reads.
Unused argument offsets are ignored. No pointer to a temporary view is retained.

Before any evaluation, validation checks counts, result capacity, layout fields,
(count-1)*stride+record_size overflow and pointer range overflow. Outputs, error
and completed must be disjoint from each other, the layout descriptor and the
entire record span, including stride gaps. Detected overlap writes nothing.
Ordinary structural failures clear completed and execute nothing. The caller
provides actual readable/writable objects of the declared sizes; numerical range
checks do not establish pointer provenance or in-process memory isolation.

A zero count accepts NULL records/results, still validates the layout and handle,
and performs no invocation or compilation. Zero-sized records, zero stride and a
NULL base are also valid for nonzero counts if arguments and view are empty.
Incorrect arity is rejected before execution. This interface does not accept
overlapping fixed records (stride smaller than record_size).

Records execute in order with the same successful-prefix contract as descriptor
batches. The failed and later outputs stay unchanged, completed names the first
failure, and error.offset is an instruction index. Loads skipped by program
control flow remain skipped. All borrowed bytes and layout values are immutable
until return. Policies, sampling, fallback, pins and statistics use the same
runtime path. No per-record heap allocation or descriptor array is constructed.
The shared native loop selects descriptor or record input setup; the integer
program lowering and error exits are shared. Its saved RBX holds the optional
record layout and RBP the stride, with R12..R15 retaining batch control.

`recode_record_bench` measures all six checked workloads, all seven batch counts
and strides 33, 64 and 129 with deliberately unaligned arguments. It compares C,
scalar calls, descriptor batches and direct records. Execution-only rows reuse
prepared descriptors. The one-request column includes descriptor/argument/view
conversion for that path and scalar argument loads inside its execution; record
calls traverse existing caller records. Caller buffer allocation/input generation,
registration, compilation and checks are excluded consistently from warm timing.
The separate cold study measures runtime creation, compilation and cleanup.
Descriptor bytes include converted arguments, views and items (56 per record).
These are allocated-size counts, not per-path RSS measurements. No hardware-cache
counters are collected; varying strides measure locality effects indirectly.
