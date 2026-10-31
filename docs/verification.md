# Control-flow milestone verification

Measured on Linux x86-64 with an Intel N150 (four cores, 700 to 3600 MHz reported
by lscpu), GCC 16.2.1, Clang 23.1.4, and CMake 4.4.5. Frequency, CPU affinity,
and scheduling were not controlled. These results describe synthetic functions,
not arbitrary applications.

## Builds and regression tests

The following configurations pass all four CTest cases: control_flow, failures,
translator, and demo. Builds use C11 with `-Wall -Wextra -Wpedantic -Wconversion
-Wshadow -Wstrict-prototypes -Werror`.

```sh
cmake -S . -B build-gcc -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build-gcc -j4
ctest --test-dir build-gcc --output-on-failure
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang
cmake --build build-clang -j4
ctest --test-dir build-clang --output-on-failure
cmake -S . -B build-sanitize-gcc -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DRECODE_SANITIZE=ON
cmake --build build-sanitize-gcc -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize-gcc --output-on-failure
cmake -S . -B build-sanitize-clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DRECODE_SANITIZE=ON
cmake --build build-sanitize-clang -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize-clang --output-on-failure
```

ASan/UBSan instrument the C implementation, not emitted instructions. Clang's
function-type check is disabled because generated functions lack its metadata.
Leak detection is disabled due to the host tracing limitation established during
the first milestone. These runs do not establish leak freedom. Linker wrappers
separately count live executable-memory allocations and verify cleanup on every
injected allocation failure.

The original systematic arithmetic/immediate tests, 1,000 straight-line programs
with 32 arguments, and 10,000 random decoder inputs remain. The old rejection of
EB was replaced with rejection of unsupported E3; trailing RET error offsets now
point to the first unreachable instruction, as documented in the translator API.
No arithmetic assertions were weakened.

New tests cover all six branch forms, forward/backward edges, nested conditions,
multiple returns, terminating loops, ADD/SUB wrapping, both CMP imm32 forms, TEST,
MOV preserving flags, and zero-delta folding with live flags. CFG cases include
entry/backedge joins, undefined RAX/ZF on the first iteration, a reverse-ordered
512-block cycle, explicit successors, invalid IR indexes, and unreachable code.
An exhaustive target sweep over a small function checks every byte for a valid
instruction boundary. Every nonempty truncated prefix of each added multibyte
form is decoded immediately before a guard page.

Exact output tests check backward and forward rel32 patching, required sizes,
untouched undersized buffers, round-trip targets, and a rel8 input expanding to a
180-byte emitted displacement. Failure injection visits every malloc in both
compilation modes, including emission after mmap, and verifies optimizer/output
objects remain unchanged when analysis allocation fails. mmap, mprotect, munmap,
combined failures, retained ownership, and RW-to-RX permissions are also tested.
The allocation length must survive an emission error even if rollback fails.

A fixed-seed generator creates 1,000 48-instruction forward-control-flow programs
with 16 arguments each, including branches, arithmetic, comparisons, and register
resets. Another 200 countdown programs vary arithmetic inside loops with two
positive arguments each. Both modes are checked against an interpreter that
parses source bytes independently of the decoder, IR, and optimizer. Original
source instructions are also executed from a separate RW-to-RX mapping with an
ENDBR64 prefix, leaving relative distances unchanged. Native comparisons run in
child processes with 20-second alarms. CTest also has process timeouts. A separate
child intentionally executes `jmp -2` and must die from its one-second alarm;
the runtime itself has no iteration cap. Process isolation here is test machinery,
not a general sandbox.

## Independent encoding and static checks

GNU as/objdump confirmed representative short and near JMP/JZ/JNZ, backward
branches, CMP's sign-extended imm32, TEST, and ENDBR64. Reproduce with:

```sh
cat > /tmp/recode-encodings.s <<'ASM'
.intel_syntax noprefix
.text
.global encodings
encodings:
    endbr64
    mov rax, rdi
.Lloop:
    sub rax, 1
    jnz .Lloop
    cmp rax, -2147483648
    test rax, rax
    jz .Lret
    {disp32} jmp .Lloop
    {disp32} jz .Lret
    {disp32} jnz .Lloop
.Lret:
    ret
ASM
as --64 /tmp/recode-encodings.s -o /tmp/recode-encodings.o
objdump -d -Mintel /tmp/recode-encodings.o
```

For example, objdump reports `75 fa` targeting offset 7 from offset 11,
`48 3d 00 00 00 80` as CMP with 0xffffffff80000000, and `0f 85 de ff ff ff`
targeting offset 7 from offset 35. The emitter's exact-byte tests separately check
canonical imm32 arithmetic and rel32 distances after layout.

GCC's analyzer and Clang's static analyzer completed without diagnostics:

```sh
mkdir -p /tmp/recode-analyzer
for file in src/ir.c src/decoder.c src/optimizer.c src/codegen.c src/runtime.c; do
    gcc -std=c11 -Iinclude -Isrc -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Werror -fanalyzer -c "$file" \
        -o "/tmp/recode-analyzer/$(basename "$file" .c).o" || exit
done
clang --analyze -std=c11 -Iinclude -Isrc -Xanalyzer -analyzer-output=text \
    src/ir.c src/decoder.c src/optimizer.c src/codegen.c src/runtime.c
```

An initial GCC analysis could not establish that the leader-building loop always
creates block zero. Constructing entry explicitly made that invariant visible
and removed the diagnostic. A broader clang-tidy run with `clang-analyzer-*`,
`bugprone-*`, and `cert-*` produced advisories about signed enum bitmask constants,
fixed shift counts, similarly typed helper parameters, the required _GNU_SOURCE
macro, bounded memcpy, and the function-pointer representation copy. Inspection
found no correctness defect in those cases. No diagnostic was suppressed to
change a score. cppcheck is unavailable.

## Synthetic benchmarks

Command: `timeout 60 ./build-gcc/recode_bench` in the Release build. Both modes
use the same emitter/runtime. Each compiles 1,000 times, timing decoding, CFG
allocation/analysis, optional optimization, layout/emission, mmap, and mprotect,
but excluding release. Execution uses 10,000,000 independent calls per round,
five rounds with alternating order. Timings include the caller loop, indirect
call, and checksum addition. Every input 0 through 31 is checked individually
before timing; each timed round's checksum is checked against independent C
calculations. Input cycles through 0 to 31 in all workloads.

- arithmetic: 32 ADD 7/SUB 6 pairs, returning x + 32.
- conditional: TEST and a short JZ select return value 7 or 9.
- loop32: initialize 32, decrement and JNZ until zero, returning zero.
- paths: CMP with 0 and 1 selects among three paths and two returns.
- flags: ADD 1/SUB 1, then MOV 77, then JZ selects 7 or 9 using the earlier ZF.

The final recorded run produced:

| Workload | Compile us, off/on | Bytes, off/on | Median ns/call, off/on | Range ns, off/on |
| --- | ---: | ---: | ---: | --- |
| arithmetic | 6.144 / 6.720 | 392 / 14 | 10.403 / 2.284 | 10.207-10.867 / 2.032-2.365 |
| conditional | 3.285 / 4.479 | 28 / 28 | 2.120 / 2.183 | 2.084-2.147 / 2.074-2.404 |
| loop32 | 3.383 / 3.962 | 22 / 22 | 20.200 / 11.633 | 20.104-20.914 / 11.230-12.490 |
| paths | 3.564 / 3.976 | 43 / 43 | 2.609 / 2.726 | 2.570-2.995 / 2.576-2.815 |
| flags | 4.367 / 5.284 | 45 / 39 | 2.421 / 2.408 | 2.370-2.709 / 2.400-2.571 |

All checksums matched: arithmetic 475000000, conditional/flags 70625000,
loop32 0, paths 88750000. Optimization increased compilation time in this run;
conditional and paths median execution also regressed slightly. An earlier run
measured compilation off/on at 5.225/5.001, 3.403/4.006, 3.206/4.020,
3.569/4.055, and 3.354/4.004 us respectively. Its loop32 medians were
10.651/10.732 ns, so the second run's large loop difference is not stable
evidence of improvement. Folding can select ADD -1 instead of SUB 1; emitted
sizes alone do not establish identical instruction streams. Frequency, placement,
and scheduling effects were not isolated. No speedup in real applications or
stable translation-cost advantage is claimed.

## Post-commit quality review

After committing the implementation, benchmarks, and documentation, reused the
isolated Python 3.14 `.venv-desloppify` installation. Its direct_url metadata
identifies `https://github.com/aasul/desloppify.git`, requested revision
`feat/c-native-analysis`, commit `5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`.
Read the installed bundled SKILL.md before using its findings. This is the fork
with the C grammar changes, not the PyPI release or upstream main.

```sh
PATH="$PWD/.venv-desloppify/jscpd/node_modules/.bin:$PATH" \
    .venv-desloppify/bin/desloppify --lang cxx \
    --exclude .venv-desloppify --exclude build-gcc --exclude build-clang \
    --exclude build-sanitize-gcc --exclude build-sanitize-clang \
    scan --path . --no-badge
.venv-desloppify/bin/desloppify --lang cxx next
.venv-desloppify/bin/desloppify --lang cxx show '*' --status open
```

The scan listed 22 open items. Seven orphaned-file reports miss linkage through
header declarations and CMake executable/library targets. Seven untested-module
reports miss CTest callers and the separately verified benchmark. Two
unreachable-code reports point to the `failed:` labels in decoder.c and ir.c;
those labels are reached by goto and are covered by malformed-input and injected
failure tests. Five stale-exclude notices concern only the virtual environment
and generated build trees. No project source was excluded.

The remaining size advisory concerns the 541-line control-flow test file. Its
shared interpreter, native-comparison fixture, directed cases, and randomized
cases belong to one regression suite; splitting it solely to lower a file-size
metric would add test plumbing without changing coverage or behavior. Inspection
confirmed no defect from that advisory. No finding was marked fixed, no source
was changed to improve a score, and no cleanup commit was needed.

The fork still uses incomplete C/C++ analysis. cppcheck was unavailable, so its
cppcheck phase did not run and security coverage fell back to regex heuristics.
GCC/Clang analyzers and clang-tidy provided the C-specific checks described above.
All 20 subjective dimensions remained unassessed, recorded by the tool as zero;
no subjective scoring review was performed. The reported overall/objective/
strict/verified values were 13.5/54.0/13.7/54.0. These numbers include the discovery
limitations and unassessed dimensions and do not establish code quality or
correctness. A repeat scan after documenting this review retained the same
findings.
