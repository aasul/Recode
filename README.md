# Recode

Recode is an experimental dynamic binary optimizer for Linux x86-64, written in
C11. It translates a restricted machine-code function with the System V AMD64
signature `uint64_t f(uint64_t)`: decode bytes, build an accumulator IR, simplify
arithmetic, emit native instructions, and execute from a protected code buffer.
The current contract preserves only the returned value.

## Build and run

GCC or Clang, CMake 3.16+, and a Linux x86-64 environment that permits executable
anonymous mappings are required. There are no external library dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/recode_demo
ctest --test-dir build --output-on-failure
./build/recode_bench
```

The demo translates `mov rax,rdi; add rax,7; sub rax,2; nop; ret` into an entry
marker followed by `mov rax,rdi; add rax,5; ret`. It prints `f(37) = 42`.
The public interface is in [include/recode.h](include/recode.h).

## Supported input

Only these exact encodings are accepted. `id` is a little-endian four-byte
immediate; `ib` is a one-byte immediate.

| Instruction | Encodings |
| --- | --- |
| `mov rax,rdi` | `48 89 f8`, `48 8b c7` |
| `mov eax,imm32` | `b8 id` |
| `add rax,imm32` | `48 05 id`, `48 81 c0 id` |
| `sub rax,imm32` | `48 2d id`, `48 81 e8 id` |
| `add rax,imm8` | `48 83 c0 ib` |
| `sub rax,imm8` | `48 83 e8 ib` |
| `nop` | `90` |
| `ret` | `c3` |

Writing EAX zero-extends into RAX. Arithmetic immediates are sign-extended to
64 bits, and arithmetic wraps modulo 2^64. RAX must be initialized by a supported
MOV before arithmetic or return. Exactly one final `ret` is required; trailing
bytes are rejected. Unsupported prefixes, registers, addressing modes, and other
encodings are rejected, including alternate encodings of these mnemonics.

The optimizer removes NOPs and combines a run of ADD/SUB operations when the
combined delta fits one sign-extended ADD or SUB immediate. A zero delta removes
the run. Otherwise it retains the arithmetic. The emitter encodes the IR anew;
it does not copy input bytes. Emitted functions begin with ENDBR64 for indirect
entry on systems with CET IBT. This output marker is not an accepted input
instruction. See [docs/translator.md](docs/translator.md) for ownership and runtime
details.

## Verification

CTest covers exact decoding and emitted bytes, truncated instructions against a
guard page, invalid IR, immediate boundaries, wrapping, optimization limits,
register resets, RX permissions, and injected allocation/system-call failures.
It also compares optimized and unoptimized execution against independent C
calculations for systematic cases and 1,000 fixed-seed random programs with 32
arguments each. Another 10,000 random byte sequences exercise decoder errors.

Select a compiler with `-DCMAKE_C_COMPILER=gcc` or `clang`. For sanitizers:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DRECODE_SANITIZE=ON
cmake --build build-sanitize -j
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure
```

Sanitizers instrument the C implementation, not the emitted instructions.
Clang's function-type sanitizer is disabled because emitted functions lack its
compiler metadata. If LeakSanitizer fails under process tracing, run CTest with
`ASAN_OPTIONS=detect_leaks=0`; that run does not check leaks.

The benchmark reports compilation time separately from execution. It compares
unoptimized translation with optimized translation of 64 arithmetic operations,
checks the results, and alternates execution order over five rounds. It does not
measure existing applications. Recorded results and limitations are in
[docs/verification.md](docs/verification.md).

## Limitations

No ELF loading, branches, memory operands, syscalls, arbitrary binaries, process
instrumentation, code-cache management, or cross-architecture execution are
implemented. Flags, exception equivalence, timing, and other CPU state are outside
the translation contract. This is not a sandbox for running arbitrary native
code. Executable-memory policy failures are reported to the caller.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Coding
agents should also follow [AGENTS.md](AGENTS.md).
