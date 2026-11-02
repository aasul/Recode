# v1.7 release checklist

This is release preparation for maintainer review. The immutable comparison
baseline is v1.6.0 at 0e4a9cca12a45dd5964ee4e9108455878b3256de.

- [x] Publish the four feature milestones as independently reviewable stacked PRs.
- [x] Preserve source contracts, default policies, exact prefix failures and sample positions.
- [x] Verify native ABI, operation semantics, relocation, guard pages and register pressure.
- [x] Exercise concurrent calls, cache pressure, pins, mapping/allocation failures and cleanup retry.
- [x] Add the installed contiguous-rules consumer for static/shared packages.
- [x] Prepare 1.7.0 metadata with API 1 and ABI/SONAME 1.7.
- [x] Retain milestone raw measurements and their regressions/limitations.
- [ ] Complete clean-archive GCC/Clang release/static/shared/ASan/UBSan/TSan verification.
- [ ] Run leak-enabled suites and a bounded fuzz smoke where supported.
- [ ] Rebuild unchanged v1.0, v1.5 and v1.6 consumers against 1.7 installations.
- [ ] Check all shared exports and SONAME, retaining every old public symbol.
- [ ] Finish immutable final comparative measurements, including available LLVM/MIR adapters.
- [ ] Complete post-commit quality review and record all five PRs/CI states.

Maintainer actions after development:

- [ ] Review and merge the five PRs in order, adjusting bases if necessary.
- [ ] Independently verify merged main and its release archive.
- [ ] Separately authorize tagging and GitHub release publication.

Evidence belongs in release-verification-1.7.md and performance-1.7.md.
Development verification does not replace maintainer release review.
