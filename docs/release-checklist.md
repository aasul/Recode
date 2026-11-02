# v1.6 release checklist

The candidate requires maintainer review. No merge, tag or release publication
is part of this assignment. The immutable baseline is v1.5.0 at 9f49d55.

- [x] Publish profiling, register allocation, batch and compilation milestones as stacked PRs.
- [x] Preserve scalar/batch contracts, policies, pins, W^X publication and executable ledgers.
- [x] Retain seven-round raw measurements and optional real LLVM ORC/MIR adapters.
- [x] Keep native error offsets, fail-fast completion and failed/later outputs unchanged.
- [x] Expand alias, pressure, backedge, sampling, differential and failure regressions.
- [x] Prepare 1.6.0 metadata with API 1, ABI 1.6, SONAME 1.6 and same-major discovery.
- [x] Complete final clean-archive compiler/sanitizer/static/shared/TSan matrix.
- [x] Rebuild unmodified v1.0 and v1.5 consumer sources against 1.6 static/shared installs.
- [x] Complete final immutable baseline/candidate performance and optional fuzz smoke.
- [ ] Record post-commit quality review, all five PRs and current CI results.

Maintainer actions after development:

- [ ] Review and merge the five PRs in their intended order, adjusting bases as needed.
- [ ] Independently verify merged main and the intended release archive.
- [ ] Separately authorize tagging and release publication.

Evidence and limitations belong in [release-verification-1.6.md](release-verification-1.6.md).
Completing development checks does not replace maintainer release approval.
