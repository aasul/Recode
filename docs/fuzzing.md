# Optional bounded fuzzing

Clang libFuzzer is a development dependency only. Normal builds neither build nor
link this harness. Use a static library without ThreadSanitizer:

```sh
cmake -S . -B /tmp/recode-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug \
  -DRECODE_BUILD_FUZZERS=ON -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF
cmake --build /tmp/recode-fuzz --parallel 2
mkdir -p /tmp/recode-corpus
cp fuzz/corpus/* /tmp/recode-corpus/
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/recode-fuzz/recode_fuzz /tmp/recode-corpus -runs=100000 -max_len=512 -timeout=5
```

The harness caps input at 512 bytes. Byte zero chooses decoding, raw accumulator
IR or integer builder/finalization/private-emitter validation. Decoded/validated
programs execute with a 256-instruction interpreter budget. Native execution is
restricted to strictly forward control flow, so it cannot loop. The driver
compares native results/status/fault positions and unchanged failure outputs,
checks emission capacity/sentinels and compares optimized accumulator results.
Looping input is validated and interpreted within the budget, never run natively.
LibFuzzer's process timeout is a harness backstop, not a runtime deadline.

Seed files cover decoder arithmetic, raw IR, memory loads, forward branches and
an invalid opcode. Integer wire format: after the selector, a 19-byte header
contains register-count-minus-one, arity, two little-endian arguments and one
reserved byte; each following 14-byte instruction contains op/dst/left/right,
u16 target and u64 immediate. Builder input deliberately permits invalid fields.
Reproduce a saved failure with `recode_fuzz /path/to/crash-file -runs=1` and retain
an independent regression test before changing implementation.

ASan/UBSan instrument C, not generated code. Guard-page and ABI tests remain
necessary. Passing a bounded campaign is evidence about those inputs, not a
security guarantee or a sandbox claim. Deterministic normal CTests retain the
larger integer/reference and guarded-memory differential suites.
