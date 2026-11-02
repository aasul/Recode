# Integer batch verification

Implementation source: 67815565bc6f58b91ad486cc2c2d8c65404d4b8b. Measurements were
built from a fresh Git-free archive of that revision, with a separately installed
Release package and independently built examples/batch-rules consumer. GCC 16.2.1,
Intel N150, Linux 7.3.0-4-cachyos, -O3 -DNDEBUG. Latency/kernel affinity CPU 0;
thread scaling CPUs 0..3. Frequency and background load uncontrolled. Three rounds
alternate scalar and batch order. No local build/test jobs overlap collection.

Commands after archive extraction and Release installation:

```sh
cmake -S examples/batch-rules -B /tmp/batch-consumer -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/recode-batch-immutable/prefix
cmake --build /tmp/batch-consumer --parallel 2
taskset -c 0 /tmp/batch-consumer/batch-rules --bench
taskset -c 0 ./build/recode_batch_bench
taskset -c 0-3 ./build/recode_batch_bench --threads
./build/recode_dispatch_probe
```

[Raw results](../benchmarks/competitors/results/batch/) include cold execution,
cold initialization/registration totals, warm timing, tier counts, code/executable
bytes and diagnostic operation counts. The bounded record fixture is shared by
source between the independent installed consumer and internal native-kernel
benchmark, and checked against a separate C calculation. Input preparation and
reference validation are outside the timed region for both scalar and batch.
The kernel diagnostic additionally compares optimized C through a volatile function
pointer. Both kernel modes check statuses and write the same result slots.

FIXED warm ns per record, medians (all sample values are retained):

| Records per request | Scalar | Batch | Batch / scalar |
| --- | ---: | ---: | ---: |
| 1 | 59.356 | 80.529 | 1.357 |
| 8 | 60.094 | 31.988 | 0.532 |
| 32 | 58.543 | 23.989 | 0.410 |
| 128 | 61.615 | 23.215 | 0.377 |
| 1024 | 60.498 | 23.913 | 0.395 |
| 4096 | 60.693 | 24.445 | 0.403 |
| 65536 | 63.131 | 27.942 | 0.443 |

At 128 records, measured FIXED throughput is about 43 million records/s versus
16 million scalar evaluations/s. This fixture supports using moderate warm batches;
one-item batches regress. Large inputs add application-array/cache costs.
INTERPRET at 128 records improves from 104.876 to 65.217 ns per record in median.
ADAPTIVE at that size has mixed tier choices, including an interpreted sample.
At 65,536 records ADAPTIVE regresses from 62.880 scalar to 67.490 batch ns/record;
its conservative model often keeps interpretation. No default thresholds were
changed to force a favorable result. Tier counts are part of the raw evidence.

The first batch is interpreted in full. FIXED scalar calls can compile during
their cold loop. A very large cold batch can therefore lose to scalar execution;
end-to-end cold totals must be read separately from warm medians. The cost model
merges per-item timing samples at batch completion and selects only at boundaries.
Scalar decisions are unchanged. This is not a universal batching or adaptive win.

The instrumented warm FIXED/INTERPRET probe verifies 10,000 records use two mutex
acquisitions, 1,250 clock reads and zero allocations, versus 20,000 mutex acquisitions
for 10,000 scalar calls. Sampling remains per item; ADAPTIVE can add a policy clock
or compile at the request boundary. No lock is held while executing records.
The native kernel remains substantially below public batch cost: validation,
iteration and sampled timing still matter. No native pointer is exposed publicly.

Shared-handle FIXED addition, median wall ns per record including thread startup,
join and result checking (128 records/request, 32,768 records/thread):

| Threads | Scalar | Batch |
| --- | ---: | ---: |
| 1 | 77.543 | 24.107 |
| 2 | 273.139 | 11.362 |
| 4 | 411.877 | 25.683 |

Four-thread results are variable on this four-core host. Batching reduces mutex
contention, but neither this fixture nor the single-host timings prove linear
scaling. Per-request arrays are application memory and are not charged to the
executable ledger. The runtime makes no batch heap allocation; native mappings
retain the page-isolated RW-to-RX ownership model and budget.

Verification executed:

- GCC Release and Clang shared Release: all 14 CTests pass in each configuration,
  including relocated static/shared consumers and exported-symbol/SONAME checks.
- GCC/Clang ASan/UBSan: batch, memory, integer, adaptive and adaptive_failures pass
  (5/5 each). Final added alias/failure tests rerun in both sanitizer builds (2/2).
- GCC TSan: the same five focused suites pass, including concurrent memory views
  and batch aliases racing invalidation, clear and cache pressure. Final alias and
  failure cases rerun (2/2). Generated native code is not sanitizer-instrumented.
- Tests cover zero/one/4,096 items plus the checked 65,536-record benchmark, exact
  arity, nulls, size/address overflow, output alias rejection, all three policies,
  independent C/scalar results, first/middle/final load faults, guards, partial
  slots/counts, repeated calls, aliased first interpretation and cache eviction.
- Mapping/protection/rollback failures fall back; failed cleanup retains ownership.
  A blocked native batch proves call-long BUSY behavior after its first result,
  and an unrelated compilation under a one-page budget cannot evict its code.
- GCC -fanalyzer and Clang --analyze on adaptive.c produce no diagnostics.
- Completed implementation committed before pinned C-support desloppify scan/next.
  CMake linkage produces orphan/coverage false positives; the parser treats `if`
  as a signature. The large existing runtime file is a maintainability advisory,
  not a reproduced correctness fault. cppcheck is unavailable and the earlier
  full jscpd scan failed; follow-up scans skip slow duplicate detection. No source
  exclusion, numeric-score claim or unrelated refactor substitutes for tests.

Remaining limits: caller pointers/declared lengths must describe real objects;
managed programs may not terminate; long batches delay eviction/removal; results
are a successful prefix, not an atomic transaction. See the exact public contract
in batch-execution.md. Old scalar entrypoints and enum values remain available.
