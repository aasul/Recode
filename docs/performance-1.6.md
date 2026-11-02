# v1.6 performance engineering

## Baseline and method

The baseline is merged main 9f49d55296614cadeaadd7338b60bfac642a53fb,
which identifies as 1.5.0. There is no v1.5 tag; GitHub's latest published release
was v1.0.0 when checked. No release was created for this work.

The profiling harness uses the comparison suite's six programs and independent C
references. It checks every output after timing, forms inputs before timing, and
separates native kernels, public scalar calls, public batches and interpretation.
Batch sizes are 1, 8, 32, 128, 1024, 4096 and 65536. Each cell executes 131072
records. Seven fresh processes per library rotate engine order; revision order
alternates. Both baseline labels use identical immutable library code to expose
measurement variation. CPU affinity is pinned to the first available CPU;
frequency, thermal state and background activity remain uncontrolled. No local
builds or tests overlap collection. See the retained environment and raw samples
in [profiling/results/baseline](../benchmarks/profiling/results/baseline).

`instructions.csv` counts static disassembled instructions, explicit stack loads,
stack stores and branches. These are not dynamic operation counts or hardware
counter measurements. RET's implicit stack read is excluded. The loop body's
operations execute repeatedly. No perf executable is installed on this host.

Baseline medians at 128 records, ns/record:

| Program | Kernel | Public scalar | Public batch | Bytes | Instructions | Stack loads/stores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| arithmetic | 4.314 | 60.664 | 21.986 | 81 | 16 | 4/5 |
| bitwise | 4.728 | 59.702 | 25.729 | 119 | 26 | 10/7 |
| conditional | 5.112 | 64.628 | 21.933 | 140 | 30 | 8/7 |
| branches | 16.896 | 69.483 | 34.788 | 850 | 199 | 65/34 |
| loop8 | 16.069 | 67.110 | 36.537 | 138 | 26 | 6/8 |
| records | 7.769 | 57.242 | 22.813 | 275 | 60 | 4/9 |

These timings differ from the published v1.5 table because this experiment uses
preformed inputs and a different call loop. Compare identical harnesses across
revisions, not these medians against unlike published boundaries.

Compilation probe medians, ns per operation:

| Program | Validate | Query including validate/layout | Emit including validate/layout | Map, emit, RX publication and free |
| --- | ---: | ---: | ---: | ---: |
| arithmetic | 124.81 | 166.93 | 202.96 | 7026.54 |
| branches | 934.80 | 1230.15 | 1600.91 | 7401.25 |
| records | 285.93 | 377.49 | 466.49 | 6231.33 |

Each stage repeats 100 times. Query and emission timings include their own
validation. Map timing includes emission, mmap, mprotect and munmap; it does not
isolate individual system calls or equal the runtime's compilation statistics.
Page-rounded executable use is 4096 bytes for each program, irrespective of text.

## Confirmed costs and next experiments

1. Public dispatch dominates tiny kernels. The linker-wrapped diagnostic observes
   20000 mutex acquisitions and 1250 clock reads per 10000 warm scalar FIXED calls,
   versus two mutex acquisitions and 1250 clock reads per 10000-record batch.
   Neither warm path allocates. Counts include sampling; wrapper timing is not
   used as latency evidence. Hash lookup/tier selection occur once per batch.
2. The integer emitter stores every write, including values immediately reloaded.
   RAX forwarding eliminates some left loads but never those stores. The arithmetic
   assembly has nine explicit stack memory operations for only two arithmetic ops.
3. Batch code computes a sampling modulo and saturating arithmetic for every
   record, even when no clock is read. It reloads descriptors and enters the native
   function for each item. Sampling semantics must survive any chunked fast path.
4. Validation/layout repeat in query and mapping. The stage probe establishes a
   measurable cost, especially for branch-heavy programs. Executable publication
   and cleanup are included in the larger map interval; no unsupported claim about
   individual syscall dominance follows from that aggregate.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
mkdir /tmp/recode-native
./build/recode_integer_profile /tmp/recode-native
objdump -D -b binary -m i386:x86-64 /tmp/recode-native/arithmetic.bin
./build/recode_dispatch_probe
sh benchmarks/profiling/compare.sh 9f49d55 HEAD /tmp/recode-profile-comparison
```

The comparison script builds archives of both immutable libraries, then compiles
the same current harness against each static library and private header set. It
uses seven rounds by default. Its output preserves raw timings, exact SHAs,
environment and disassembly. The harness is private tooling, not an installed API.
The existing optional LLVM/MIR collector retains cold, lifecycle, concurrent and
bulk comparisons; `BENCH_ROUNDS` now defaults to seven for compare-revisions.sh.
Normal library builds still require neither external compiler.
