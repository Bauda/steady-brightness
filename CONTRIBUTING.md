# Contributing

Thanks for helping make monitor brightness control more dependable. Reports, documentation, accessibility checks, and small focused patches are all welcome.

## Before starting

Search existing issues. Use the bug report or feature request form for a new topic. For a large change, discuss the problem in an issue before investing in implementation. You do not need permission to open a small fix.

For monitor reports, include the model, Windows version, GPU/driver, connection path, DDC/CI setting, and what happened. Remove device identifiers and personal paths from reports before posting. Do not attach a full system dump.

## Develop locally

1. Fork and clone the repository, then create a branch.
2. Install MSVC x64 C++ Build Tools and a Windows SDK (Desktop development with C++).
3. Run `build.cmd`. The executable and intermediates go into `build/`.
4. Run `build\SteadyBrightness.exe --demo` for simulated monitors.
5. Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test.ps1`.

See [testing](docs/testing.md) for desktop visual tests and safe manual hardware checks. Test scripts require Windows PowerShell 5.1; the application itself has no .NET dependency.

## Design constraints

- Keep idle work event-driven. Avoid recurring idle timers, telemetry, network requests, background animations, and new runtime dependencies.
- Keep potentially blocking driver calls in the helper. Preserve timeout, read-back verification, bounded retries, handle cleanup, and independent monitor values.
- Preserve per-user installation, safe removal, existing startup/theme preferences, keyboard access, UI Automation, and high-contrast behavior.
- Prefer narrow patches. Match surrounding code rather than reformatting unrelated files. Add or adjust a regression check when it exercises meaningful behavior.
- Keep source, screenshots, and diagnostics free of credentials, private data, and machine-specific paths.

## Pull requests

Explain the problem, the resulting behavior, and how you verified it. Include screenshots for visual changes and clearly separate simulated tests from real monitor results. Hardware support claims should name the setup actually tested. CI builds and tests on Windows; passing CI alone does not establish hardware compatibility.

Contributions are provided under the repository's MIT license; keep existing third-party notices. No separate contributor agreement is required. Maintainers review changes as time permits; there is no response-time guarantee.

Follow the [Code of Conduct](CODE_OF_CONDUCT.md). Security problems should be [reported privately](SECURITY.md).
