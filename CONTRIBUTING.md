# Contributing to Recode

Recode is an experimental dynamic binary optimizer for Linux x86-64, written in C. The code needs to be understandable, correct, and testable before it needs to be clever.

Contributions are welcome, whether you write them yourself or use AI tools.

## AI-assisted contributions

**AI tools are allowed. The contributor is responsible for the entire patch.**

Before submitting code produced or edited with AI:

- Read every line of the final diff. If you cannot explain what a line does, why it is needed, and what assumptions it makes, do not submit it.
- Understand the control flow, ownership, memory operations, error handling, and architecture-specific behavior. Running tests is necessary, but it does not replace understanding.
- Verify generated APIs, instruction semantics, documentation claims, and build commands against the actual project or authoritative references.
- Remove code you cannot justify, including speculative branches, duplicate helpers, unused abstractions, and tests that do not test the stated behavior.
- Check that you have the right to contribute any copied or adapted third-party code. Do not paste code with an incompatible license.
- Be prepared to answer review questions and maintain or correct the code after it is merged.

The same standards apply to code written without AI. We do not reject patches just because AI was used, and we do not accept them just because an AI tool reported success. You do not need to add an AI disclaimer to source files or commit messages. Do not misrepresent how changes were tested or reviewed.

## Before writing code

Check the README, existing code, open issues, and related pull requests. For a new subsystem or a change that affects the execution model, open an issue to discuss the approach before investing in a large patch.

Keep pull requests focused. Separate a bug fix, an unrelated cleanup, and a new feature unless they are genuinely dependent.

## C and systems rules

- Target C11 and Linux x86-64 unless a change explicitly extends that scope.
- Use explicit integer widths where representation matters. Check lengths, offsets, alignment, overflow, and allocation failures.
- Keep ownership and cleanup clear. Avoid hidden global state and implicit lifetime assumptions.
- Decode untrusted bytes defensively. Never silently treat an unknown instruction or unsupported CPU feature as supported.
- When translating instructions, preserve architectural effects: registers, flags, memory accesses, control flow, and relevant fault behavior. State limitations clearly.
- Handle executable memory deliberately. Do not introduce writable-and-executable mappings by default. Explain any exception.
- Prefer small, purposeful functions over layers of wrappers and abstract interfaces with one implementation.
- Add dependencies only when the benefit justifies the maintenance and portability cost.
- Write regression tests for bug fixes and targeted tests for new behavior. Cover errors and boundary cases, not only the happy path.
- Do not weaken tests, suppress warnings, or change expected outputs merely to make a patch pass.
- Treat measured performance as evidence. Provide baseline numbers, hardware details, repeatable commands, and overhead or regressions when making speed claims.

## Writing rules

Write plainly, like a developer explaining real code to another developer.

- Use direct names and descriptions. Explain what changed and why.
- No em dashes, marketing language, exaggerated claims, or generic AI-style introductions and conclusions.
- Do not fill comments with restatements of the next line. Comment on invariants, instruction semantics, non-obvious decisions, or trade-offs.
- Avoid boilerplate comments and docstrings that add no information.
- Do not describe features as complete, safe, fast, or compatible without evidence.
- Keep README and PR text factual. Do not invent benchmarks, users, review outcomes, or test results.
- Do not add AI-generated signatures, self-referential remarks, or attribution boilerplate to normal source comments.

Readable code is the goal. Hiding AI use or trying to pass an AI detector is not.

## Working on a change

1. Create a branch from an up-to-date `main`.
2. Implement the requested change completely before splitting it into commits.
3. Add relevant tests and run the checks available for the affected code. Include compiler warnings and sanitizers where practical.
4. Review your complete diff for accidental files, unrelated edits, and unexplained code.
5. Split the validated result into small, logical commits. Each commit should be understandable and preferably work on its own.
6. Open a pull request with the problem, approach, actual test commands and results, and known limitations.
7. Respond to review with technical explanations and follow-up changes.

Use concise, lowercase commit subjects, for example:

```text
feat(decoder): support rip-relative addressing
fix(runtime): reject invalid code-cache offsets
test(ir): cover flag preservation
docs: explain code-cache lifetime
```

Do not force-push over someone else's work. Do not include local environments, generated build output, or files unrelated to your change.

There is no standard build or test command established yet. Follow the commands documented by the repository as they are added. Until then, report exactly what you ran rather than claiming a nonexistent test suite passed.

## Pull request review

A pull request should make it possible for another contributor to verify the change without guessing what happened. Include:

- The problem and why the chosen approach solves it.
- The important behavior changes and any risks.
- Tests that were actually run, with their results. Identify checks you could not run.
- Reproduction steps for bugs, and benchmark methodology for performance claims.
- Any unsupported instructions, platforms, or known failure cases.

Maintainers may ask you to explain any part of a patch, whether it was AI-assisted or not. If you cannot verify or defend a behavior, revise the patch before asking for a merge.

## Code-quality tools

Code-quality tools are useful evidence, not a substitute for review.

For C-aware scans, Recode's agent workflow currently points to the author's desloppify fork:

https://github.com/aasul/desloppify/tree/feat/c-native-analysis

This is the branch associated with https://github.com/peteromallet/desloppify/pull/910. It improves grammar selection for C files but does not provide complete native C analysis. Treat findings as candidates to verify, and do not change code just to improve a score.

See [AGENTS.md](AGENTS.md) for the optional tool setup and the post-implementation review workflow followed by coding agents.

## Working with other contributors

Keep issue and PR discussions focused on the code. Be respectful, credit existing work, and disagree with technical arguments rather than personal remarks. Review contributions by behavior and evidence, not by whether someone used AI.

Maintainers may close out-of-scope, unreviewable, or untested submissions. If a change is rejected, the review should explain the technical reason.
