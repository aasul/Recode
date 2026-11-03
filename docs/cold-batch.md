# Opt-in cold batch compilation

Set `rc_runtime_config.cold_batch_compile = true` on a default-initialized
configuration to allow early FIXED/ADAPTIVE batch compilation. The default is
false. INTERPRET never compiles. Scalar entry behavior is unchanged. Existing
source rebuilds without edits; the added trailing configuration field requires
matching minor-version headers and library, as permitted by the 1.x policy.

On first batch use, a cached alias can execute immediately without compilation.
Otherwise the runtime estimates work as instruction count plus one for each
LOAD or MUL. The initial cost model uses 8+4*work ns for interpretation and
4+work ns for native execution. It requires requested_count*(4+3*work) to exceed
twice the compilation-cost estimate. Checked division avoids multiplication
overflow. The estimate starts at initial_compile_ns (default 50 us) and follows
the existing runtime-wide measured compilation-cost average after successes.

This bounded heuristic uses program shape and the current request only. It
assumes no future calls, so expected_reuse/horizon prediction is not applied to
an unseen handle. It does not predict loop trips or early faults. It can decline
profitable loops or compile a request that fails early. The margin is a measured
engineering choice, not a universal profitability threshold. Configure the
initial estimate for a deployment and measure its workloads.

A cold estimate cannot evict existing code. There must be unused executable
budget, and the exact page-rounded prepared size is checked before mapping.
Compilation/publication retain the existing mutex and ownership protocol.
Concurrent callers therefore publish one translation; expensive compilation
still serializes this runtime. Allocation, mapping, publication or capacity
failure executes through the interpreter and preserves last_compile_error and
existing item-count retry cooldowns. Structural preflight precedes any attempt.
No invalid native entry is exposed.

After any interpreted history, existing FIXED/ADAPTIVE decisions apply. Native
slowdown retirement requires actual interpreted samples; an opt-in native-only
handle has no measured interpreter baseline to compare against. Sampling and
attempted-item counters, including bounds failures, retain their meanings.

`recode_cold_bench` checks six workloads and all seven batch sizes. It includes
runtime creation/registration, execution/compilation and destruction; caller
input preparation and program construction are outside both paths. Statistics
queries and independent result checks are also outside timing. This is not a
measurement of application input conversion. The same harness can link v1.6
without RECODE_HAS_COLD_BATCH; both opt_in labels then execute its default path.
