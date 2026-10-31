# Working on Recode

These instructions apply to every coding agent working in this repository.

## Project

Recode is an experimental dynamic binary optimizer for Linux x86-64, written in C. The long-term goal is to improve selected workloads on resource-constrained computers without spending more memory or CPU time on optimization than the work saves.

Treat correctness as a prerequisite for performance. Do not claim support for arbitrary binaries, transparent execution, or speedups until tests demonstrate them. Keep platform and instruction-set support explicit.

## Workflow

1. Read the relevant source, tests, build configuration, and existing conventions before editing.
2. Identify the smallest complete implementation that solves the requested problem. Do the implementation, not just a plan or a diagnostic report.
3. Finish the requested change before preparing commits. Do not commit half-written code, placeholders, or knowingly broken intermediate states just to show activity.
4. Add or update meaningful tests and run the applicable checks. Investigate failures instead of changing tests to make them pass.
5. Review the completed diff for correctness, unnecessary changes, readability, and accidental artifacts.
6. Split the validated work into small, logical commits. Stage only the relevant files or hunks, and review each staged diff.
7. After the implementation is committed, run the code-quality workflow below. Fix confirmed issues in follow-up commits, then rescan.
8. Finish with a concise summary of the work, commit hashes, tests actually run, quality findings addressed, and remaining limitations.

Work autonomously within the requested scope. Don't stop after finding a bug if a fix and test are feasible. Do not silently expand the scope into unrelated refactors.

## C and systems code

- Prefer plain C11 with explicit, narrowly scoped Linux and x86-64 extensions where needed.
- Use clear names, straightforward control flow, and small functions with a real purpose.
- Avoid speculative frameworks, unnecessary wrappers, redundant helpers, and abstractions that merely rename a single operation.
- Make ownership, lifetimes, buffer sizes, alignment, integer widths, error paths, and resource cleanup obvious.
- Check boundaries when decoding instructions or handling untrusted executable input. Do not assume input bytes are valid instructions.
- Preserve architectural state and observable behavior when changing execution or translation code. Never trade correctness for a benchmark result.
- Test failure paths and edge cases. Use compiler warnings, sanitizers, and independent tools where applicable.
- Measure performance against a defined baseline, with reproducible workloads. Report regressions and runtime overhead as well as gains.
- Never silently weaken an assertion or remove a regression test to accommodate an implementation.

## Writing and comments

Write like a careful developer maintaining a public codebase.

- Use direct, specific language in comments, documentation, issues, pull requests, and commit messages.
- Do not use em dashes or decorative punctuation.
- No marketing copy, filler, generated-sounding introductions, or exaggerated claims.
- Do not add comments that simply repeat what the next line does.
- Comment on non-obvious invariants, tricky CPU behavior, intentional limitations, and decisions a maintainer would otherwise have to rediscover.
- Do not add AI-related disclaimers, commentary about generating code, or boilerplate section headings inside source files.
- Match existing style where it is sensible. Prefer readable, maintainable code over clever code.

## Git history

The repository history should explain how the implementation evolved.

- Complete and validate the requested update first, then commit the finished changes in small, coherent groups.
- Each commit should have one understandable purpose and preferably build and pass its relevant tests on its own.
- Use `git diff`, `git diff --check`, `git status`, and `git diff --cached` before committing. Use `git add -p` or explicit paths rather than `git add .` when unrelated changes exist.
- If a change cannot be split without breaking intermediate commits, keep that dependency together. Do not manufacture tiny commits at the expense of a usable history.
- Do not include unrelated user changes, local environments, generated build files, or quality-tool state.
- Use short, lowercase, informative commit subjects, for example `feat(decoder): handle rip-relative operands`, `test(runtime): cover code-cache eviction`, or `fix(ir): preserve carry flag on exit`.
- No vague subjects such as `updates`, `improvements`, or `fix stuff`. Avoid generic commit bodies.
- Leave no intended source changes uncommitted at the end. If work cannot be completed or validated, disclose it rather than committing broken code.
- Never rewrite, reset, or force-push other people's commits. Never commit directly to `main` unless the user explicitly requests it.
- When a remote and permissions are available, publish completed work to the current feature branch if the task calls for a published update. Do not force-push.

## Desloppify: post-implementation review

Run desloppify **after** the requested work is implemented, tested, and committed, not as a substitute for completing the task.

Upstream: https://github.com/peteromallet/desloppify

C parsing improvements under review: https://github.com/peteromallet/desloppify/pull/910

PR #910 is not full native C support. It improves C/C++ Tree-sitter grammar selection in the shared `cxx` plugin. Findings may be incomplete or incorrect for C code. Do not claim otherwise, and do not treat a numeric score as proof of code quality.

Until upstream has suitable C support, use the C-support branch in the author's fork:

https://github.com/aasul/desloppify/tree/feat/c-native-analysis

This is the branch behind PR #910, not the upstream `main` branch or the published PyPI release. Install directly from `feat/c-native-analysis` so local checks use the C parsing changes. For repeatable CI or when debugging a regression, pin a tested commit instead. The revision previously verified to contain the patch is `5179f6ba61dca2050813a1aa7e2f71ba7e4ce765`.

Keep the tool in a separate Python 3.11+ virtual environment and do not add it as a runtime dependency of Recode. Example setup and scan:

```sh
python3 -m venv .venv-desloppify
.venv-desloppify/bin/python -m pip install --upgrade pip
.venv-desloppify/bin/python -m pip install "desloppify[full] @ git+https://github.com/aasul/desloppify.git@feat/c-native-analysis"
.venv-desloppify/bin/desloppify --lang cxx scan --path .
.venv-desloppify/bin/desloppify --lang cxx next
```

Read the tool's current instructions before relying on individual findings. Exclude only obvious generated output, vendor code, and build directories. Do not exclude project code to inflate a score.

For each actionable finding:

1. Reproduce or inspect the issue in the actual C source.
2. Reject false positives, documenting the reason if useful.
3. Fix real problems without changing unrelated behavior.
4. Run the applicable build and tests.
5. Commit the cleanup as its own logical change.
6. Rescan to confirm the result.

If desloppify is unavailable, the fork fails to install, or its C analysis does not cover the issue, report the limitation and proceed with compiler warnings, relevant tests, sanitizers, and available C-specific tools such as clang-tidy or cppcheck. Do not claim a scan passed when it did not run.

Quality tooling must not alter the project's behavior just to improve a score.
