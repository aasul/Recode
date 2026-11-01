# Supported platform

The first stable contract targets Linux x86-64, 64-bit pointers/size_t, System V
AMD64 LP64 calling conventions, default structure packing and 32-bit C enums.
The integer memory view is little-endian. No optional SIMD extension is required.
Generated entry markers use ENDBR64, which is a no-op without CET. Stack frames,
callee-saved registers, RW-to-RX publication and unaligned widths are covered by
native ABI/guard-page/differential tests.

| Environment | Evidence |
| --- | --- |
| Intel N150, Linux 7.3.0-4-cachyos, x86-64, glibc | GCC 16.2.1, Clang 23.1.4; Release, ASan/UBSan, GCC TSan; static/shared clean-archive consumers |
| Ubuntu x86-64 GitHub Actions runners | Compiler/sanitizer, shared ABI/integration and bounded fuzz jobs on each stacked PR; exact run links accompany PRs |
| Other Linux x86-64 environments meeting this ABI | Intended contract, without claiming every kernel/libc/toolchain combination was tested |
| Non-Linux, x86-32, ARM/AArch64, alternate ABIs | Unsupported; CMake rejects incompatible system/architecture/pointer size |

Build requirements: CMake 3.16+, a GCC or Clang C11 compiler, libc and POSIX threads.
Tests additionally need the compiler's x86-64 assembler; shared ABI checks use
GNU-compatible nm/readelf. Python is used only by optional benchmark summarization
and quality tooling, not the library. Clang libFuzzer and compiler sanitizers are
optional development tools. No external runtime package beyond libc/threads is
introduced. The stated CMake minimum follows used features; local tests use a
newer CMake, not an assertion that every version/toolchain combination was run.

Native compilation requires anonymous mmap, mprotect from RW to RX, and munmap.
No RWX request is made. Hosts may deny executable memory: managed policies fall
back to interpretation, preserving execution errors; low-level compilation reports
the OS failure. See troubleshooting.md. There are no filesystem/network operations
in normal library execution beyond OS memory/timing/thread facilities.
