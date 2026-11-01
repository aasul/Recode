# Embedding Recode

Recode is an experimental C11 library for Linux x86-64. Install an optimized
build into a prefix you own:

```sh
cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DBUILD_TESTING=OFF -DRECODE_BUILD_TOOLS=OFF
cmake --build build-install --parallel 2
cmake --install build-install
```

The default library is static. Add `-DBUILD_SHARED_LIBS=ON` for a shared library.
Both variants export the same `Recode::recode` target, installed headers, version
file and relocatable CMake package. The target propagates C11, include paths and
`Threads::Threads`. Private warning flags are not exported. Sanitizer builds are
for development; use ordinary builds for distribution. Do not mix an instrumented
static library with an application lacking the corresponding sanitizer runtime.

A separate application needs only:

```cmake
cmake_minimum_required(VERSION 3.16)
project(application LANGUAGES C)
find_package(Recode CONFIG REQUIRED)
add_executable(application main.c)
target_link_libraries(application PRIVATE Recode::recode)
```

Configure it with `-DCMAKE_PREFIX_PATH=/your/prefix`. If moving a shared package,
configure the consumer again for its new location. Deployment must make the
shared library available to the dynamic loader, for example with an application
install RPATH or the system library search path.

The complete [embedding application](../examples/embed/main.c) uses only public
headers and demonstrates configuration, registration, checked execution,
statistics, removal and cleanup. Build it independently after installation:

```sh
cmake -S examples/embed -B build-consumer -DCMAKE_PREFIX_PATH="$HOME/.local"
cmake --build build-consumer --parallel 2
./build-consumer/embed
```

CTest's `embedding-OFF` and `embedding-ON` each build and install from a clean
nested build, move the prefix, build this consumer and run it. They also check
exports for source/build paths. They intentionally install uninstrumented builds,
including when invoked from a sanitizer test tree.

Keep the runtime and registered handles across calls to amortize setup. Source
bytes are borrowed only until registration returns. Successful registration owns
validated IR; freeing or modifying the original bytes cannot change execution.
A handle is nonzero and belongs to its creating runtime. Removal invalidates that
handle permanently. Equivalent registrations can share a translation; removing
one does not remove its aliases. Never pass a handle to another runtime.

`RC_POLICY_INTERPRET` never compiles. `RC_POLICY_FIXED` compiles after a configured
call threshold. Default `RC_POLICY_ADAPTIVE` estimates whether measured savings
can repay compilation, with cooldowns and conservative reuse bounds.
`RC_POLICY_TRACE` records hot accumulator loop paths and uses guarded native
traces plus interpreter continuation. It can be slower than whole-function JIT.
Compilation/capacity failures fall back to interpretation and appear in
`rc_function_stats.last_compile_error`; they do not make a successful execution
fail. A zero executable budget permits interpretation but prevents native code.

API status values are returned directly; optional `rc_error` supplies an offset
and system errno. Execution errors leave the result unchanged. Numeric output
handles remain unchanged on failed registration. Initialize pointer outputs to
NULL and low-level decoded/code objects to zero. `rc_status_string` gives a static
string requiring no release. Statistics are copied snapshots, not live objects.

Managed API operations synchronize internally and execute code outside the cache
mutex with lifetime protection. `remove` and `invalidate` can return `RC_BUSY`.
Join callers and exclude all new API entry before destruction, including threads
waiting to acquire a runtime lock. On success destruction frees everything and
sets the runtime pointer to NULL. Failed mapping release retains ownership for
retry; do not discard the runtime pointer. APIs are not signal-safe,
cancellation-safe, or reusable after multithreaded fork before exec.

Low-level `rc_compile` is separate: it returns a caller-owned native mapping,
which must remain alive until every call ends. Never free a borrowed `rc_program`
array with `rc_program_free`. The managed runtime exposes no native code pointers.
The installed `examples/data-rules` consumer demonstrates record filtering with
a borrowed data buffer, independent C results and explicit bounds errors.
See [memory-views.md](memory-views.md) for its contract.
For two-input register programs, use the [integer API](integer-programs.md).
See [runtime.md](runtime.md), [translator.md](translator.md) and
[compatibility.md](compatibility.md) for the full contracts.

`RECODE_VERSION` and its component macros describe the installed headers;
`rc_version_string()` identifies the linked library. `RECODE_API_VERSION` detects
incompatible source-contract revisions. Additive changes aim to preserve v0.4
source compatibility. There is no stable binary ABI: public structs may grow,
and clients must rebuild against matching headers and library. CMake versioned
requests accept only the same minor release. The SONAME now includes major/minor (0.9), separating incompatible minor ABIs.
The [proposed API freeze](public-api.md) describes the v1.0 contract.
