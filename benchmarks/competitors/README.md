# Integer JIT comparisons

This is a separate installed-package consumer. Recode's default build has no
LLVM or MIR dependency. All adapters lower the same `workloads.c` instruction
arrays: unsigned 64-bit wrapping arithmetic, bitwise operations, a conditional,
16 data-dependent branches, an eight-iteration loop, and bounded read-only record
filtering. Independent C functions check 1,024 individual results, boundary bit
patterns and each measured checksum. Inputs are identical across engines. C may
simplify equivalent operations; equivalent outputs do not imply equal CPU work.
The timed loop includes input formation, dispatch and checksum accumulation.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/recode-prefix
cmake --build build --parallel 2
cmake --install build
cmake -S benchmarks/competitors -B build-comparison -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/recode-prefix
cmake --build build-comparison --parallel 2
ctest --test-dir build-comparison --output-on-failure
python3 benchmarks/competitors/collect.py build-comparison /tmp/comparison-results \
  --revision "$(git rev-parse HEAD)" --cpu 0
python3 benchmarks/competitors/summarize.py /tmp/comparison-results/raw.csv
./build-comparison/lifecycle
```

Optional adapters use actual public compiler APIs:

- [LLVM ORC LLJIT](https://llvm.org/docs/ORCv2.html): configure
  `-DCOMPARISON_LLVM=ON`. Needs llvm-config, llvm-c headers and libLLVM. The adapter
  builds mutable registers with allocas, verifies the module, runs `default<O2>`,
  transfers a ThreadSafeModule to LLJIT and resolves the entry with LLJITLookup.
  Lookup materializes native code. Tested with LLVM 23.1.4. The pre-23 context
  API is selected at compile time but has not been tested here.
- [MIR](https://github.com/vnmakarov/mir): configure `-DCOMPARISON_MIR=ON`,
  `-DMIR_INCLUDE_DIR=/path/to/mir` and `-DMIR_LIBRARY=/path/to/libmir_static.a`.
  Build upstream separately with CMake target `mir_static`. The adapter emits MIR
  text, calls MIR_scan_string, MIR_load_module, MIR_link and MIR_gen at optimization
  level 2. The recorded revision is in results/v10/environment.json. No source is
  vendored. Parsing/finalization includes validation and is reported in build_ns;
  standalone validate_ns is zero because no separate pass is measured.

`compare`, `compare-llvm` and `compare-mir` are separate executables. Recode/C runs
do not load libLLVM or MIR. A missing adapter is omitted by the collector, never
relabelled. Do not use a build directory containing stale optional binaries when
measuring dependency availability. This comparison project needs CMake 3.19+;
Recode itself retains its 3.16 minimum.

Each measurement cell runs in a fresh process. Three rounds alternate engine
order. Counts are 1, 10, 100, 1,000, 10,000 and 100,000. The first call is measured
separately, then the cold loop runs without additional warmup. end_to_end_ns covers
initialization, construction, finalization, registration, the first call and the
cold loop. Explicit LLVM/MIR compilation occurs before the first call; Recode
compiles according to the selected policy during execution. Recode FIXED uses a
threshold of two but every new handle still starts interpreted. compile_ns is
Recode's sampled compiler interval or the adapter's explicit optimization,
materialization and linking interval, not a universal equivalent compiler phase.
Recode validate_ns also includes copying to its immutable finalized program.

The steady phase follows 1,024 checked warmup calls. It must not be confused with
the cold loop. Statistics include the first, cold, verification and steady phases.
Short adaptive programs can remain interpreted. Multithreaded warm time includes
thread creation/join and reports total wall time for calls*threads invocations;
inputs/checksums are per thread. This measures shared-handle contention, not just
kernel scaling. No unstable timing threshold gates CI.

CLOCK_MONOTONIC measures wall time; zero-work timer calibration is not subtracted.
Tiny-count samples have substantial noise. The collector records compiler versions,
CPU, kernel, affinity, rounds and dependency revision. Frequency and system load
are uncontrolled. Thread scaling runs inherit the collector's affinity: use an
unpinned separate run with --scaling-only to measure multiple CPUs. Linux /proc/self/status VmHWM supplies peak_rss_kib since exec, including
libraries and compilation temporaries, not incremental JIT memory. rusage_peak_kib
is also retained: ru_maxrss can include the Python launcher's pre-exec memory floor,
so it must not be used as incremental engine memory. executable_bytes is Recode's tracked page-rounded peak, not RSS. code_bytes
is emitted Recode code or LLVM .text sections, excluding linker stubs and padding.
MIR code size and LLVM/MIR executable allocation usage are -1 (not measured): their
public interfaces used here do not provide comparable counters. Zero C compiler
costs mean ahead-of-time work is outside this experiment.

`lifecycle` checks 4,096 distinct functions with ten calls and actual removal of
each registration in a retained Recode runtime. Existing `recode_integer_bench
--pressure` retains distinct translations under a 16-page budget, including checked
eviction/clear accounting; these are Recode-only lifecycle/cache experiments,
not equivalent LLVM/MIR lifetime comparisons. `recode_trace_bench` measures the
accumulator representation, including interpreter side exits. Integer programs
cannot use TRACE; no integer comparison is labelled as tracing.

Raw results and their environment are committed. The v1.0 source revision is the
exact v1.0.0 tag, 35028f5bc2167e6662e67802099c7d6c5d4dcce2. Build that revision
from `git archive` and compile this unchanged comparison consumer against its
installed package to reproduce the baseline. Reports explain missing metrics,
regressions and practical limits; these results do not establish general superiority.
