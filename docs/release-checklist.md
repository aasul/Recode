# v1.5 release checklist

The candidate requires maintainer review. Do not merge, tag or publish without
explicit authorization. The v1.0.0 tag is the immutable baseline.

- [x] Implement benchmark, measured emitter optimization and integer batching as stacked PRs.
- [x] Preserve existing scalar signatures, enum values, ownership and default policy settings.
- [x] Document batch preflight, alias, prefix failure, lifetime, statistics and tier boundaries.
- [x] Keep LLVM/MIR optional and retain raw favorable and unfavorable measurements.
- [x] Preserve isolated RW-to-RX mapping publication and executable accounting.
- [x] Pass batch-stage GCC/Clang, ASan/UBSan, TSan and installed static/shared checks.
- [x] Verify final 1.5.0 headers/package, 31 exports and SONAME 1.5 from a clean archive.
- [x] Run final clean-archive GCC/Clang Release, shared and ASan/UBSan matrix plus supported TSan.
- [x] Rebuild unchanged v1.0 external consumer sources against installed 1.5.
- [x] Complete final reproducible v1.0 versus v1.5 measurements and optional fuzz smoke.
- [x] Record final post-commit C-support quality review and compiler diagnostics.
- [ ] Publish and check all four PRs with correct stack bases and passing CI.
- [x] Keep MIT licensing and unsupported platform/sandbox limits explicit.

Maintainer release actions, intentionally pending:

- [ ] Review/merge benchmark, runtime, batch and release PRs in that order, retargeting later bases to main.
- [ ] Rerun final verification on merged main and the intended release archive.
- [ ] Explicitly authorize the v1.5.0 tag and GitHub release publication.

A completed development checklist is not maintainer release approval. Historical
v1.0 evidence remains in release-verification.md; current candidate evidence in release-verification-1.5.md identifies exact source revisions and any unresolved blockers.
