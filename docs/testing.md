# Testing

## Automated checks

Build first with `build.cmd`, then run from Windows PowerShell 5.1:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test.ps1
```

The runner uses separate processes for integration and UI Automation checks. It launches simulated monitors and deliberate helper failures, checks independent values, hide/reopen behavior, GDI object growth, crash isolation, and the nine-second timeout. It never invokes the hardware write diagnostic or presses Refresh in fault modes. Tests find windows by their own process ID, so they do not target an already running copy.

For input/painting and light/dark persistence checks on an unlocked, interactive Windows desktop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test.ps1 -Visual
```

Visual checks compare live slider painting against a full repaint, exercise direct track clicks, dragging, cancellation, endpoints, keyboard input, disabled controls, themes, and accessible values. They temporarily change the app's theme preference and restore it afterward. Do not interact with the test flyout or run multiple suites concurrently. Pixel checks require an unobscured desktop and are not included in hosted CI. Windows PowerShell provides the test harness's .NET UI Automation assemblies; these are not app runtime requirements.

Results and screenshots go into ignored `test-results/`. GitHub Actions builds on Windows, runs nonvisual checks, and verifies release packaging. CodeQL separately scans the C++ code. CI is not a hardware compatibility test.

## Manual hardware checks

Record the original brightness of each monitor before testing. Use the UI to change one monitor by a small amount, check the monitor's own menu or a separate read-only probe, verify other monitors are unchanged, then restore the original value. Test light/dark/high contrast, Windows scaling, sleeping/waking monitors, Refresh, and closing/reopening the flyout. Only claim results actually observed.

The `--test-hardware <path>` diagnostic deliberately writes a small brightness step to each supported monitor and attempts to restore it. Run it only on hardware you are authorized to adjust, while monitors are awake and you can manually restore brightness if a driver fails. This direct diagnostic has no UI helper timeout. It is excluded from automated checks.

For idle measurements, close the flyout, wait for the worker to exit, and record elapsed time, process CPU time, private memory, working set, handles, and GDI objects. Working set includes shared memory. Compare equivalent conditions and report the sampling duration; a short zero-CPU sample is not proof of zero lifetime CPU use or a 24-hour endurance result.
