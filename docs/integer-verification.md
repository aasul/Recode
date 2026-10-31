# v0.6 execution verification

Environment matches [v0.5 verification](integration-verification.md): Intel N150,
Linux x86-64, GCC 16.2.1, Clang 23.1.4. Ten CTest cases pass in GCC Release,
Clang Release, GCC/Clang ASan/UBSan Debug and GCC shared Release. Focused integer,
trace, adaptive and adaptive_failures cases pass GCC TSan Debug. External tests
install clean static/shared builds, relocate each prefix, and independently build
and run both embedding and rules applications. Rules check three separate C
expressions over 4,900 boundary input pairs per rule and policy.

Commands actually run:

```sh
cmake -S . -B build-v06-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-v06-gcc --parallel 2
ctest --test-dir build-v06-gcc --output-on-failure
cmake -S . -B build-v06-clang-OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build build-v06-clang-OFF --parallel 2
ctest --test-dir build-v06-clang-OFF --output-on-failure
# gcc and clang, separate build-v06-<compiler>-ON directories:
cmake -S . -B build-v06-gcc-ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_SANITIZE=ON
cmake --build build-v06-gcc-ON --parallel 2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-v06-gcc-ON --output-on-failure
cmake -S . -B build-v06-shared -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build-v06-shared --parallel 2
ctest --test-dir build-v06-shared --output-on-failure
cmake -S . -B build-v06-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_THREAD_SANITIZE=ON
cmake --build build-v06-tsan --parallel 2
ctest --test-dir build-v06-tsan -R '^(trace|adaptive|adaptive_failures|integer|integer_failures)$' --output-on-failure
```

The new suite tests all 14 binary operations with 121 boundary pairs each,
including register 63, destination/source aliases, arithmetic wrapping, high-bit
signed ordering and shifts 0, 63, 64, 65 and UINT64_MAX. Independent signed expected
values use int64_t bit representations; the interpreter instead orders sign-bit
biased unsigned values. 300 deterministic 64-instruction programs, with mutable
registers and conditional edges, use 32 input pairs each. A separate walker and
C semantic switch check both interpreter and emitted execution. The assembly
harness sets RBX/RBP/R12..R15 sentinels and verifies preservation for every native
comparison. Bounded loops test backward relocation, initialization and budget
exhaustion; there is no runtime termination guarantee.

Builder tests cover argument/register/operation/unused-field errors, invalid
branches, undefined join/backedge reads, unreachable code, fallthrough, output
reuse and the instruction limit. Runtime tests cover zero/one/two inputs, wrong
counts, wrong representation, interpretation/fixed/adaptive results, explicit
TRACE rejection, zero budget fallback, multipage capacity rejection, copied
ownership, aliases, field-sensitive identity, eviction, invalidation, removal,
repeated calls and six callers racing clear/invalidation. Existing accumulator
and exact trace snapshot regressions remain enabled.

Wrapped failure tests sweep every observed builder/finalization/registration heap
allocation, including growth retaining prior instructions. Integer emission after
finalization does not allocate temporary heap memory. Native failure tests inject
mmap, mprotect and munmap failures, check fallback answers, retained rollback
accounting, failed invalidation disabling native lookup, retry cooldown and eventual
cleanup. Live allocation/mapping counters return to zero. Shared builds still
exercise wrapping through the dedicated static test library.

GCC -fanalyzer with actual object compilation reported three uninitialized-register
warnings in the integer interpreter: it assumes an arbitrary operation can be
first, without modeling the opaque program's finalized definite-assignment
contract. Source review and rejection regressions verify that public construction
cannot produce those paths. The first syntax-only invocation did not exercise
full GCC analysis; it was replaced with object compilation. GCC analysis was also
rerun on an exported v0.5 tree: all eight original units passed warnings as errors.
The new emitter and changed runtime had no GCC analyzer diagnostics.
Clang --analyze reported one must-analysis warning in integer.c: it assumes a
branch can reach an uninitialized facts entry. Source review confirms all count
entries are initialized before propagation and every successor is validated to be
within count (the external entry facts are explicitly zero). These warnings are
contract/loop-model false positives; no warning suppression, redundant register
initialization or behavior change was used. Normal builds still treat compiler
warnings as errors. The other changed units had no Clang analyzer diagnostics.
GNU as/objdump independently verified stack addressing,
IMUL, CMP/SETcc/MOVZX, shifts and frame restoration encodings.

A separate adversarial pass traced allocation bounds, must-analysis convergence,
entry/backedge facts, operand captures, ABI scratch registers, stack frame balance,
rel32 targets at instruction slots, representation-sensitive identity, publication
only after RX, call-long ownership and failed release. No raw native entry is
exposed by the new public API. Sanitizers cover C, not generated machine code;
LeakSanitizer is disabled as in CI. No human review or proof of race freedom is
claimed. There is no global program/metadata quota, sandbox, stable ABI or native
execution budget.

Post-commit quality scan/next/show used the same isolated C fork at 5179f6b,
verified from its install metadata. It displayed 45 open reports with seven more
hidden by the detector noise budget. New reports concern the explicitly linked
integer modules and executed external application, local `require` signatures,
test/runtime size, and short mapping/protection/rollback sequences shared with
the trace emitter. Each pipeline publishes a different typed ABI; combining those
small sequences would add callback/ownership machinery without fixing a defect.
The existing importer/goto/exclusion reports retain the v0.5 explanations.
Source inspection found no confirmed defect from these reports. No project code
was excluded and no finding was hidden or marked fixed to affect a score. Rescan
reproduced the same reports. C discovery is incomplete and cppcheck unavailable;
compiler analyzers, executed tests and manual ABI/ownership review remain necessary.
