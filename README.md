# Recode

Recode is an experimental dynamic binary optimizer for Linux x86-64, written in
C11. It translates a restricted machine-code function with the System V AMD64
signature `uint64_t f(uint64_t)`: decode bytes, build an accumulator IR and
control-flow graph, simplify arithmetic, emit native instructions, and execute
from a protected code buffer.
The contract preserves branch decisions and the returned value.

## Build and run

GCC or Clang, CMake 3.16+, and a Linux x86-64 environment that permits executable
anonymous mappings are required. There are no external library dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/recode_demo
ctest --test-dir build --output-on-failure
timeout 60 ./build/recode_bench
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
| `cmp rax,imm32` | `48 3d id`, `48 81 f8 id` |
| `test rax,rax` | `48 85 c0` |
| `jmp rel8/rel32` | `eb ib`, `e9 id` |
| `jz rel8/rel32` | `74 ib`, `0f 84 id` |
| `jnz rel8/rel32` | `75 ib`, `0f 85 id` |
| `nop` | `90` |
| `ret` | `c3` |

Writing EAX zero-extends into RAX. Arithmetic and CMP immediates are sign-extended
to 64 bits. Arithmetic wraps modulo 2^64. ADD and SUB set ZF from the result;
CMP sets ZF from equality without changing RAX, and TEST sets ZF when RAX is zero.
MOV preserves flags. JZ takes its target when ZF is set; JNZ takes it when clear.

Functions can contain multiple blocks, return paths, forward/backward branches,
and loops. Every branch must target an instruction boundary inside the supplied
buffer. All decoded blocks must be reachable in the graph; padding and unreachable
instructions are rejected. Reachable paths must initialize RAX before reading it
and ZF before a conditional jump. Validation intersects facts from all reachable
predecessors, including backedges. Falling through the buffer end is rejected.
A closed loop without a RET is valid and may run forever. The runtime imposes no
iteration limit. Unsupported prefixes, registers, addressing modes, and alternate
encodings remain rejected.

The optimizer removes NOPs and folds ADD/SUB runs within a basic block when the
combined delta fits one sign-extended immediate. A nonzero folded delta preserves
ZF because the final RAX is unchanged. A zero delta becomes ADD 0 when ZF remains
live, including across MOV, branches, joins, and loops; otherwise it is removed.

The emitter lays out blocks in input order and resolves fixed-width rel32 branches
after calculating instruction offsets. Generated functions begin with ENDBR64 for
CET IBT entry. This output marker is not accepted as input. Short input branches
and imm8 arithmetic can grow during emission. Compilation uses RW then RX memory,
never RWX. See [docs/translator.md](docs/translator.md) for the IR, error handling,
ownership, and compatibility details.

## Verification

CTest covers instruction encodings, guard-page truncations, malformed branches,
initialization through loops and joins, ZF-sensitive optimization, relocation,
wrapping, RX permissions, and allocation/system-call failures. It retains the
straight-line tests and adds 1,000 deterministic forward-control-flow programs
and 200 countdown programs. Optimized and unoptimized execution are compared
with a separate source-byte interpreter and original native execution in timed
child processes. This test isolation does not provide a runtime sandbox.

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

The benchmark reports compilation time, generated size, and execution separately
for arithmetic, a short conditional, a 32-iteration loop, multiple paths, and
flag-sensitive arithmetic. Both modes check per-input results and timed checksums.
It alternates execution order over five rounds. Hardware, actual measurements,
static analysis, and tool limitations are in [docs/verification.md](docs/verification.md).

## Limitations

No ELF loading, external or indirect branches, calls, memory operands, syscalls,
arbitrary binaries, process instrumentation, code-cache management, or
cross-architecture execution are implemented. Only ZF is modeled, and only its
in-function uses are preserved. Final flags, exception equivalence, timing, and
other CPU state are outside the translation contract. Accepted loops need not
terminate. This is not a sandbox for running arbitrary native code.
Executable-memory policy failures are reported to the caller.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Coding
agents should also follow [AGENTS.md](AGENTS.md).

Report bugs, propose features, and track engineering tasks through
[GitHub Issues](https://github.com/aasul/Recode/issues). See
[docs/maintaining.md](docs/maintaining.md) for the maintainer workflow and
[SECURITY.md](SECURITY.md) for responsible vulnerability reporting. GitHub
Actions checks GCC and Clang builds and runs CTest for pull requests.
