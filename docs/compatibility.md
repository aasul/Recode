# Compatibility and execution limits

- Linux x86-64 with 64-bit pointers and the System V AMD64 ABI only.
- GCC or Clang with C11, CMake 3.16+, POSIX threads and libc. No additional runtime
  packages, accounts, network access or paid dependencies are required.
- Native compilation needs anonymous `mmap`, RW emission followed by RX
  `mprotect`, and `munmap`. No RWX mappings are requested. Policies that compile
  fall back to interpretation when executable mappings are denied. Direct
  `rc_compile` reports failure instead. Generated entries include ENDBR64.
- The [integer API](integer-programs.md) also supports 0..2 uint64_t arguments
  with explicit virtual registers. All its operations interpret and compile;
  TRACE registration is rejected for this representation. Bounded read-only
  [memory views](memory-views.md) are supported only by the integer API.
- Byte functions have signature `uint64_t f(uint64_t)`. The exact accepted input
  encodings are listed in [README](../README.md#supported-input); alternative
  prefixes/registers/encodings are not implicitly supported. There are no memory
  operands, calls, ELF loading, indirect branches, floating point or SIMD.
- The accumulator contract preserves RAX-derived returned values and ZF-dependent
  control flow. It is not general architectural-state or exception equivalence.
  Arithmetic wraps modulo 2^64. Valid loops can run forever; managed calls have no
  execution budget. `rc_interpret` provides an explicit diagnostic budget.
- Executable-byte limits cover all live page-rounded mappings, including failed
  rollback cleanup. Program and metadata bytes are reported separately and have
  no global quota. Temporary analysis allocations and allocator overhead are
  outside reported persistent usage.

Recode is not a sandbox. Validation of a restricted instruction subset does not
make arbitrary untrusted native code safe. Use process-level isolation and
resource limits for hostile input. Bugs in validation/emission remain possible;
do not expose this experimental runtime as an unrestricted execution service.
Generated machine code is not sanitizer-instrumented. See [SECURITY](../SECURITY.md).

Source compatibility and binary compatibility are separate. Existing v0.4 entry
points remain available. Rebuild consumers against each experimental release;
there is no stable ABI guarantee. The library remains MIT-licensed.
