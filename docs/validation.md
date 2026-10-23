# Validation and compatibility

These observations describe the local version 1.0.2 build validated on 2026-10-23. GitHub Actions builds a fresh executable with its runner's MSVC/SDK, so its file size and checksums can differ. They are evidence for a specific setup, not universal compatibility guarantees.

## Hardware checked

| Environment | Result |
| --- | --- |
| Windows 11 x64, NVIDIA RTX 2070 SUPER | Native tray and flyout exercised |
| Acer XV271U M3, DisplayPort | Windows DDC/CI read, slider write, independent read-back, and restoration passed |
| Samsung C27JG5x, DisplayPort | Windows DDC/CI read, slider write, independent read-back, and restoration passed |

Both monitors were awake. Each monitor's brightness was changed slightly through the actual slider, independently read back, and restored. The helper exited after the flyout closed.

Built-in panel WMI and NVIDIA direct I2C fallback are implemented but were not exercised on hardware in this setup because Windows DDC/CI succeeded. Explorer restart, suspend/resume, cable hotplug, other GPU families, Windows 10 hardware, and a 24-hour endurance run were not explicitly validated here.

## Idle sample

Flyout hidden, no helper running, one-minute local sample:

| Elapsed | Working set | Private memory | Recorded CPU time | GDI objects |
| --- | --- | --- | --- | --- |
| 20.0 s | 15.11 MiB | 2.38 MiB | 0.0 ms | 18 |
| 40.1 s | 15.09 MiB | 2.32 MiB | 0.0 ms | 18 |
| 60.1 s | 15.07 MiB | 2.29 MiB | 0.0 ms | 18 |

Windows counter resolution limits the CPU reading. Working set includes shared Windows libraries and can vary over time and between compiler builds. GPU usage was not separately benchmarked. The implementation has no app rendering loop while hidden, but Windows still performs desktop composition.

## Behavior checked locally

- Independent sliders; direct clicks; dragging and grab offset; clamping; keyboard input; cancellation and disabled states.
- Antialiased thumb with no native black artifact; live painting identical to a full repaint in the tested states.
- Light/dark/Windows theme, preference persistence, and no GDI growth over repeated interactions.
- UI Automation slider role, name, range, current value, bounded writes, and disabled read-only behavior.
- Helper crash handling, hung-command timeout, responsive tray, and recovery via Refresh.

See [testing](testing.md) for repeatable simulated tests and manual hardware methodology. Share reproducible reports through [Issues](https://github.com/Bauda/steady-brightness/issues/new/choose); hardware support remains dependent on the monitor and connection path.
