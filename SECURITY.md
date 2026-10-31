# Security policy

Recode is an experimental x86-64 translator. It is not a sandbox for untrusted machine code, and it does not provide isolation from the host process. The currently supported platform and execution contract are documented in the README.

## Reporting security problems

If you find a possible vulnerability, avoid posting a working exploit or sensitive details in a public issue.

Use GitHub's private vulnerability reporting feature under the repository's **Security** tab if it is available. If it is not available, ask the maintainer for a private reporting channel without disclosing exploit details publicly.

For ordinary crashes, unsupported instructions, or correctness bugs that do not create a security disclosure risk, use the public bug report form.

Include the affected revision, minimal reproduction, expected behavior, impact, and relevant platform details.

## Supported versions

This project is in early development and does not currently publish supported release branches. Security and correctness fixes target the latest `main`. No response or patch timeframe is guaranteed.

Security reports are assessed based on Recode's documented execution contract. Executing arbitrary untrusted native code is not a supported or secure use case.
