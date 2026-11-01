# Troubleshooting

| Symptom | Check and action |
| --- | --- |
| CMake cannot find Recode | Install first, then set CMAKE_PREFIX_PATH to that prefix. Use the exported Recode::recode target and matching headers/library. Reconfigure after relocating a shared prefix. |
| Loader cannot find librecode.so.1.0 | Install the shared object where the loader searches or configure the application's install RPATH. Build-time package discovery does not configure deployment paths. |
| Native compilation fails with SYSTEM | Inspect last_compile_error.system_errno. mmap/mprotect may be restricted by the host. Managed calls interpret; low-level rc_compile returns the failure. Keep/retry any retained cleanup mapping. |
| Native compilation fails with CAPACITY | Check executable_budget, page size and pinned mappings. A zero budget disables compilation. Increase the budget only if the application can afford it; interpreter fallback remains available. |
| UNSUPPORTED or TRUNCATED | Check the exact supported byte encodings in README. Recode does not decode arbitrary binaries. Integer TRACE registration is deliberately unsupported. |
| INVALID_IR, UNINITIALIZED or UNDEFINED_ZF | Check operands, zero unused fields, branch targets and definitions on every incoming path. error.offset identifies the failing instruction or byte position. |
| BOUNDS | The executed LOAD's width does not fit the remaining declared view. Check unsigned byte offsets and length, not native pointer values. Result remains unchanged. |
| BUDGET | A direct interpreter exceeded its diagnostic instruction count. Native/managed calls have no corresponding timeout. Isolate potentially nonterminating native programs in supervised processes. |
| BUSY during cache/lifecycle operations | An execution owns a handle or native pin. Let it finish before retrying. Stop all new API entry and join threads before destroy. |
| SYSTEM during clear/remove/destroy | Mapping ownership is retained after failed release. Preserve the runtime/code pointer, inspect errno and retry cleanup. Do not free it by another method. |
| ARGUMENT or NOT_FOUND | Check exact integer arity, descriptor validity, output initialization and handle lifetime/runtime identity. Do not use opaque pointers after destruction. |
| A sanitizer test passes but native code is suspect | Generated instructions are not sanitizer-instrumented. Run guard-page, ABI and differential regressions; preserve the failing input for a source-backed report. |

`rc_status_string` describes status values; optional `rc_error` provides position
and OS details. Successful operations clear the error. Keep execution errors
separate from nonfatal compilation diagnostics in function statistics.
