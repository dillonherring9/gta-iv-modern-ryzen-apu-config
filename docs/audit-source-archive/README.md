# User-Supplied After Dark Audit Source Material

This directory preserves the readable report and settings documents from the user-supplied `gta-iv-after-dark-audit.zip` archive. The source ZIP SHA-256 is:

```text
5694423c8bdc25657660419d409a5015130e8bf11c8390c337e9070f3697aa30
```

The archive contained PowerShell installer/uninstaller scripts and a thin NSIS wrapper. They were inspected as untrusted source and **were not executed or adopted**. Version 4 instead uses the project’s native NSIS installer because the prototype managed only four files and did not implement the existing full seven-file contract. See [`../V4_AUDIT_ADOPTION.md`](../V4_AUDIT_ADOPTION.md).

The copied text is retained for attribution, review, and future maintainers. Current Version 4 instructions take precedence wherever the audit’s 2026 claims or implementation assumptions differ from verified current sources.
