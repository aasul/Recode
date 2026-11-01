# v1.0 release-preparation checklist

The implementation checklist records checks actually performed. Maintainer actions
below are intentionally separate and are not authorization to publish.

- [x] Version/header/package metadata identify 1.0.0; stable SONAME is 1.0.
- [x] Public headers, error/ownership/fallback/thread contracts are finalized.
- [x] Exact shared symbols and ABI layout/enum policy have been reviewed.
- [x] GCC and Clang Release pass from a Git-free source archive.
- [x] GCC and Clang ASan/UBSan pass from that archive.
- [x] GCC ThreadSanitizer focused memory/lifecycle/trace suites pass where supported.
- [x] GCC and Clang shared tests and symbol/SONAME checks pass.
- [x] Static/shared installations relocate and build all independent consumers.
- [x] Guard-page, native ABI, differential, failure/rollback and lifecycle tests pass.
- [x] Optional bounded fuzz campaign and current-source security review complete.
- [x] Cold/steady performance and cache pressure are measured with variation retained.
- [x] Getting started, migration, troubleshooting, limitations and release notes reviewed.
- [x] MIT license is included in source and installed packages.
- [x] Source archive contains required files and excludes local/build/quality state.
- [x] Completed implementation committed; post-commit C-fork quality review recorded.
- [x] All three stacked PRs have passing GitHub Actions at their final heads.
- [x] No known release-blocking source defect remains unresolved.

Maintainer actions after review:

- [ ] Merge v0.8 into main, then v0.9 into main, then v1.0 preparation into main.
- [ ] Verify the merged source and intended release archive again.
- [ ] Explicitly authorize creation of v1.0.0 and publication of the GitHub release.

No merge, release tag, GitHub release or release-asset upload is performed by this
preparation. Exact revision/test evidence and limitations are in release-verification.md.
