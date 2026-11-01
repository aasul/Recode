# Security policy

Recode is a restricted in-process JIT and execution library for Linux x86-64. It is not a sandbox for untrusted machine code, and it does not provide isolation from the host process. The currently supported platform and execution contract are documented in the README.

## Reporting security problems

If you find a possible vulnerability, avoid posting a working exploit or sensitive details in a public issue.

Use GitHub's private vulnerability reporting feature under the repository's **Security** tab if it is available. If it is not available, ask the maintainer for a private reporting channel without disclosing exploit details publicly.

For ordinary crashes, unsupported instructions, or correctness bugs that do not create a security disclosure risk, use the public bug report form.

Include the affected revision, minimal reproduction, expected behavior, impact, and relevant platform details.

## Supported versions

The 1.0 branch is currently release preparation, not a published stable release.
Until publication, security and correctness fixes target the latest reviewed work
and `main`. The planned stable support line is 1.0.x on the documented Linux
x86-64 ABI. No response or patch timeframe is guaranteed.

Security reports are assessed based on Recode's documented execution contract. Executing arbitrary untrusted native code is not a supported or secure use case.

Bounded memory views check guest offsets against a caller-declared readable object;
they cannot validate an arbitrary native pointer, enforce its real lifetime, or
prevent caller data races. Programs can loop forever. The executable budget is
not a process-memory quota. Use process-level isolation and resource limits for
hostile input. Generated instructions are not sanitizer-instrumented. See
[public-api.md](docs/public-api.md) and [memory-limits.md](docs/memory-limits.md).
