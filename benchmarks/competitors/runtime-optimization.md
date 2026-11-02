# v1.5 integer code-generation study

Baseline: v1.0.0, 35028f5bc2167e6662e67802099c7d6c5d4dcce2.
Optimized implementation: 00f49de (full revision in the raw result directory).
Both were extracted from immutable Git archives and compiled with the same current
integer benchmark driver. Exact command:

```sh
taskset -c 0 sh benchmarks/compare.sh 35028f5bc2167e6662e67802099c7d6c5d4dcce2 \
  00f49de /tmp/recode-perf-immutable 3
```

Intel N150, GCC 16.2.1, Linux 7.3.0-4-cachyos, -O3 -DNDEBUG, CPU 0. No overlapping
local build/test jobs during this immutable comparison; frequency/system load
uncontrolled. Three alternating outer rounds each contain five rotating mode
samples. [Raw data](results/runtime-optimization/) retains every distribution,
call count, pressure mode, trace and accumulator result, including regressions.

The emitter uses signed disp8 addresses for virtual slots 0..15 and disp32 for
slots 16..63. RAX can forward the immediately available virtual value within a
straight-line block; every write still spills to its authoritative stack slot.
Every branch target and control transfer resets the cache. ARGUMENT stores do not
write RAX and invalidate a cached slot if they overwrite it. No register allocation,
stack ABI, memory checks, public signatures or policy defaults change.

Mixed inputs, 50,000 calls, medians in ns/evaluation. Direct kernels bypass public
lookup/pinning and are diagnostic, not an installed native-pointer API.

| Workload | Kernel before | Kernel after | FIXED before | FIXED after | Bytes before | Bytes after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 6.336 | 5.548 | 65.897 | 56.758 | 116 | 81 |
| bitwise | 6.784 | 6.058 | 61.134 | 61.874 | 178 | 119 |
| rule | 7.188 | 6.359 | 60.851 | 57.691 | 209 | 135 |
| shifts | 5.907 | 5.437 | 58.869 | 57.946 | 163 | 106 |
| branches | 23.541 | 19.568 | 73.165 | 72.306 | 1275 | 850 |

All five native kernels improved in median, approximately 8..17%, and generated
bytes fell by 30..35%. This is enough to retain the bounded local optimization.
A page-rounded executable mapping is still one page for each tiny function, so
reduced text does not imply reduced RSS or cache-budget use. The cache-pressure
counts and ownership contracts are unchanged. No shared arenas are introduced.

Compilation latency is noisy and some workloads regress. Kernel compilation
includes size query, validation, mapping, emission and RX publication:

| Workload | Compile us before | Compile us after | C ns before | C ns after |
| --- | ---: | ---: | ---: | ---: |
| arithmetic | 34.491 | 18.991 | 2.926 | 2.925 |
| bitwise | 20.145 | 24.312 | 2.925 | 2.621 |
| rule | 19.079 | 19.046 | 3.204 | 3.203 |
| shifts | 19.023 | 24.593 | 3.343 | 3.343 |
| branches | 28.733 | 32.069 | 3.343 | 2.927 |

The extra bounded planning pass can add compilation work. Bitwise FIXED warm
median also regresses slightly. Unchanged C and interpreter controls vary, so
these measurements do not support attributing every managed-call difference to
the emitter. Arithmetic's large compilation improvement is not a general compiler
latency claim. Read the raw min/max as well as the medians. Dispatch remains two
mutex acquisitions and sampled clocks per scalar call; batching addresses this
separate, observed bottleneck in the next milestone. Adaptive thresholds and
cooldowns are retained because these data do not establish a portable alternative.
perf is not installed; evidence uses controlled kernels, the wrapped dispatch
probe and disassembly instead of invented CPU-cycle attribution.

Verification executed:

- GCC and Clang Release: all 13 CTests passed for each, including failure injection,
  concurrent cache ownership and installed static/shared consumers.
- GCC ASan/UBSan: integer, memory, adaptive and adaptive_failures passed (4/4).
- GCC -fanalyzer and Clang --analyze on integer_codegen.c: no diagnostics.
- Native ABI sentinel checks, 9,600 independent random input comparisons, guard-page
  loads, all 64 slots, argument-overwrite and control-flow-join regressions passed.
- GNU objdump decoded actual emitted slots +120 as disp8 and +128 as disp32, with
  reused RAX, preserved argument registers, no callee-saved clobber and a restored
  144-byte frame on return. Generated code is not sanitizer-instrumented.
- Post-implementation commit followed by the pinned desloppify C-support scan,
  next and source inspection. Untested/orphaned CMake-linked modules are parser
  limitations; duplicated mapping cleanup reflects distinct entry types and
  existing ownership. cppcheck is unavailable. The initial full scan's jscpd
  detectors failed; subsequent objective scans skip slow duplicate detection.
  No score-driven source exclusions or unrelated refactors were made.
