# Maintainer workflow

Recode is maintained as a small public C systems project. GitHub Issues are the task tracker, pull requests are the review record, and GitHub Actions runs the build and test suite.

## Issue tracking

Use the bug report form for reproducible defects, the feature proposal form for new behavior, and the engineering task form for scoped maintenance work.

The issue title should describe an actual outcome. Use issue bodies for acceptance criteria, affected modules, and tests. Reference issues from PRs, and close an issue only when the documented work is complete.

Suggested labels to create in GitHub Settings > Labels:

- `bug`: confirmed incorrect behavior
- `enhancement`: feature request
- `documentation`: docs only
- `testing`: tests or CI
- `performance`: measured performance work
- `good first issue`: small, well-defined contribution
- `help wanted`: maintainers welcome contributions
- `needs reproduction`: insufficient evidence to diagnose

These are suggestions, not labels currently created by this document. Avoid assigning `good first issue` to work that requires broad binary-translation knowledge without guidance.

## Pull requests

Every source change should go through a feature branch and pull request. The author is responsible for reviewing every line, whether AI-assisted or not, and for the tests claimed in the description. Follow CONTRIBUTING.md and the pull request template.

Prefer focused commits with clear messages. Do not split tightly coupled changes into broken commits. Review the diff and CI result before merging. Do not merge known failing tests without documenting and understanding the problem.

## Continuous integration

The `ci` workflow runs on PRs to `main` or its explicitly listed stacked milestone
bases, pushes to `main`, and manual dispatch. It compiles clean source archives
using GCC and Clang in release and sanitizer configurations, then runs CTest.
A separate shared-library and bounded-fuzz job checks exports and installed consumers.

The expected check names are:

- `build-gcc-release`
- `build-clang-release`
- `build-gcc-sanitize`
- `build-clang-sanitize`

Verify the exact check names from a completed GitHub Actions run before configuring branch protection. Initially require the release checks. Make sanitizer checks required after confirming they are stable in GitHub-hosted CI.

The workflow disables LeakSanitizer because the documented environment could not run it reliably. This is not equivalent to passing a leak check. Run leak detection independently on a compatible host.

## Protecting main

Repository settings cannot be enforced by files in the repository. An administrator must configure a branch ruleset through GitHub Settings > Rules > Rulesets.

Recommended initial ruleset:

1. Name: `protect-main`, enforcement: Active, target: branch `main`.
2. Require a pull request before merging.
3. Require 0 approving reviews initially. A sole PR author cannot approve their own work, and a one-review requirement can block a solo project.
4. Require successful status checks: `build-gcc-release` and `build-clang-release`, after those checks have actually run.
5. Block force pushes and branch deletion.
6. Require conversation resolution before merging, if available.
7. Do not add bypass permissions routinely. Leave emergency bypass only if you explicitly choose and document it.

Avoid enabling signed-commit requirements, merge queues, or strict up-to-date checks until there is a demonstrated need. If you enable a linear-history rule, use squash or rebase merges instead of merge commits. Recode's existing PRs used merge commits, so this changes the history policy for future merges.

After saving the ruleset, open a test PR and check that GitHub prevents merging until the required checks pass. Do not describe `main` as protected until GitHub reports an active rule.

## Releases and security

Do not publish versioned compatibility or performance guarantees until the relevant tests and representative benchmarks support them. Review security reports under SECURITY.md and avoid treating executing untrusted native code as a supported use case.
