# Changelog

## 1.5.0 preparation (unreleased)

- Add fail-fast integer batch execution with borrowed per-item views and checked result prefixes.
- Amortize lookup, locking and native pinning while preserving concurrent cache ownership.
- Compact integer stack addresses and forward local RAX values without changing virtual-register semantics.
- Add optional actual LLVM ORC/MIR comparisons, immutable baselines, raw measurements and batch studies.
- Add installed batch-rules, guard/alias/partial-failure/cache-pressure tests and batch sanitizer coverage.
- Identify the rebuilt minor ABI with SONAME 1.5; retain documented 1.x source compatibility.
- Record one-item, adaptive and compilation regressions alongside measured improvements.

## 1.0.0

- Finalize Linux x86-64 C11 public source and patch-level ABI contracts, with SONAME 1.0.
- Add getting started, migration, troubleshooting, release notes and release validation.
- Validate clean source archives, static/shared installations and external consumers.
- Preserve measured interpreter/JIT overhead and security/resource limitations.

## 0.9.0

- Validate private native-emitter input before table access and entry-ABI selection.
- Restrict shared exports to public APIs and add symbol/SONAME checks.
- Add optional bounded Clang fuzzing, lifecycle stress and allocation/rollback regressions.
- Define proposed API freeze, memory accounting and caller resource responsibilities.

## 0.8.0

- Add caller-owned read-only byte views and unsigned little-endian 8/16/32/64-bit loads.
- Propagate interpreter/native bounds errors without changing result outputs.
- Add independent installed data filtering with C/interpreter/JIT comparison.
- Add guarded boundary, ABI, differential, mapping-failure and concurrent-view tests.

## 0.7.0

- Reduce unused dispatch clock reads and defer trace feedback under the completion lock.
- Emit compact integer kernels and record reproducible performance comparisons.

Earlier experimental execution, tracing and integration work remains described in
README and the verification guides. No entry here asserts a published GitHub release.
