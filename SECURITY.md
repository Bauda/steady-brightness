# Security policy

Security fixes target the latest stable release. Older versions are not maintained separately. Update before reporting a problem where possible.

## Report a vulnerability privately

Use **[Report a vulnerability](https://github.com/Bauda/steady-brightness/security/advisories/new)** in this repository's Security tab. Include the affected version, impact, reproduction steps, and a minimal example. Do not include real credentials or another person's data. If private reporting is temporarily unavailable, open an issue asking for a private contact without disclosing the vulnerability.

Please allow time for investigation and a fix before public disclosure. This is a volunteer project with no guaranteed response time or bug bounty. Ordinary monitor compatibility problems belong in the bug report form.

## Trust and scope

The app runs as the current Windows user, writes per-user settings and installation files, and communicates locally with monitor APIs. It makes no network requests and collects no telemetry. Diagnostic reports are only written to the path explicitly supplied by the user; review them for device identifiers before sharing.

Official binaries are published only in this repository's Releases. They are currently unsigned. Checksums detect corruption; GitHub provenance links release artifacts to the workflow that built them. Neither is a Windows code-signing certificate or a guarantee that code has no defects. See [download verification](docs/install.md#verify-a-download).

Workflow dependencies are pinned to commit hashes and proposed updates are managed by Dependabot. Pull-request CI has read-only repository permissions. Release publication uses a separate job with the permissions it needs. Never add tokens, certificates, local diagnostic reports, or private paths to commits.
