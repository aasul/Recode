# Recode 1.5.0 candidate

This branch prepares a minor release for maintainer review. No v1.5 tag or GitHub
release has been created. Linux x86-64, C11 and the MIT license remain unchanged.

The main addition is rc_runtime_call_integer_batch: evaluate one registered
integer program over many independent argument/view records with one handle
lookup and call-long native protection. Earlier successful results survive a
later bounds error; completed identifies the failing record while error.offset
identifies its instruction. Inputs stay borrowed through return. See
[the API contract](batch-execution.md) and the installed examples/batch-rules consumer.

Batching supports INTERPRET, FIXED and ADAPTIVE. Tier selection occurs at request
boundaries, and the first batch on each handle is interpreted. One-item requests
and large cold/adaptive batches can regress. Native pins can delay eviction and
removal. The interface provides no deadline, cancellation or sandbox isolation.

The integer emitter uses compact stack displacements and bounded local RAX
forwarding. Stack slots remain authoritative at control-flow joins, and existing
native ABI, read bounds and RW-to-RX publication rules remain. The immutable study
shows smaller code and improved native-kernel medians on five fixtures, with some
compilation and managed-call regressions retained in the report.

The optional independent comparison suite includes actual LLVM ORC LLJIT and MIR,
plus C and Recode paths. LLVM/MIR remain outside the normal library dependencies.
Measured compilation/memory costs can favor Recode, while optimized competitor
native functions and direct C usually execute faster. Batching reduces managed
per-record overhead; it does not establish general superiority over other compilers.
See [comparison methodology](../benchmarks/competitors/README.md),
[baseline](../benchmarks/competitors/baseline.md) and [batch evidence](batch-verification.md).

Existing supported v1.0 source rebuilds against 1.5. No old function or enum value
is removed or renumbered. The library exports 31 public functions. API version
remains 1; version macros/package metadata become 1.5.0 and SONAME becomes 1.5.
Cross-minor binary compatibility is not promised. Read [migration](migration.md).

Compiler sanitizers instrument C, not generated instructions. Tests include native
ABI, guarded reads, independent differential results, partial failures, mapping
rollback and concurrent cache pressure. These checks are evidence, not a security
proof. Persistent metadata/application memory and total RSS remain outside the
executable-byte budget. Optional C analysis is incomplete; limitations are retained
in the verification reports.
