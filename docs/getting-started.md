# Getting started

This is the 1.0 release-preparation candidate. Publication and tagging still need
maintainer approval. Use Linux x86-64, GCC or Clang with C11, CMake 3.16+, libc and
POSIX threads. Native execution requires anonymous mappings that can transition
from RW to RX. Interpretation works when executable mappings are denied.

Obtain the reviewed source archive, or clone and select the candidate branch:

```sh
git clone https://github.com/aasul/Recode.git
cd Recode
git checkout release/v1.0-stable-api
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Install a static library into a chosen prefix. Add `-DBUILD_SHARED_LIBS=ON` for a
shared library; both expose the same CMake target and installed public headers.

```sh
cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX=/tmp/recode-prefix
cmake --build build-install --parallel 2
cmake --install build-install
cmake -S examples/data-rules -B build-consumer -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/tmp/recode-prefix
cmake --build build-consumer --parallel 2
./build-consumer/data-rules
```

Your external CMake project needs `find_package(Recode 1.0 CONFIG REQUIRED)` and
`target_link_libraries(your_target PRIVATE Recode::recode)`. Do not add Recode's
source directory to include paths. The target supplies C11, public includes and
Threads::Threads. See [embedding.md](embedding.md) for deployment/loader paths.

The complete [data-rules application](../examples/data-rules/main.c) constructs
arithmetic and conditional rules over two uint64_t arguments and a 15-byte data
record. It uses only recode_integer.h and the installed library. Read it in order:

1. Create a builder, append explicit instructions and finalize an immutable copy.
2. Interpret with `rc_integer_interpret_view`; supply an instruction budget when needed.
3. Create a runtime with `rc_runtime_default_config`, choose INTERPRET, FIXED or ADAPTIVE,
   and register the program. Registration copies it.
4. Call `rc_runtime_call_integer_view` with arguments and borrowed data/length.
   Check status before using result; BOUNDS leaves the previous output unchanged.
5. Inspect `rc_runtime_function_stats` and `rc_runtime_get_stats`. FIXED in this
   example proves native execution via the jitted counter. ADAPTIVE can stay interpreted.
6. Remove handles, destroy the runtime after callers finish, then destroy programs.
   On cleanup SYSTEM errors retain ownership and retry; do not discard the pointer.

The example checks direct C, interpretation, native execution, truncated data and
failure outputs. `examples/embed` demonstrates accumulator byte registration;
`examples/rules` demonstrates memory-free integer rules. All build independently
against installed static/shared packages and relocated prefixes in CTest.

Read [public-api.md](public-api.md), [integer-programs.md](integer-programs.md) and
[memory-views.md](memory-views.md) before integrating. There are no raw pointer
arguments, stores, external calls, integer traces or managed/native deadlines.
Recode is not a sandbox. Direct C remains faster on the measured small workloads;
use the [release verification](release-verification.md) to assess overhead.
