Native Windows monitor brightness control with independent sliders or one linked slider for all displays.

### New in v1.0.6

- Right-click the tray icon to open Options on its own. Left-click still opens the brightness sliders.
- **Tray icon visibility settings** opens the Windows page where you can check and change icon visibility.
- Options detects a missing startup target and offers **Repair startup registration**. Installing updates an existing registration to the installed executable, and known Windows-disabled startup entries link to Startup Apps.
- Background launch briefly retries monitor detection while drivers come online. The tray can register again if Explorer rejects an icon identifier tied to a previous executable path.

Validation covers startup registration, the separate tray menu, bounded background recovery, and the Windows test and packaging suites.

### Install

1. Download **SteadyBrightness-Windows-x64.zip** below (not the source archives).
2. Extract it and run **install.cmd**, or run **SteadyBrightness.exe** for portable use.
3. Click the sun beside the clock. Enable DDC/CI in the monitor's own menu if needed.

Windows 10/11 **x64**. No administrator access or additional runtime installation required. Automatic startup is optional in Options. To update an installed copy, exit the app and run the new `install.cmd`; preferences are retained.

### Included

- Independent hardware brightness controls, direct track clicks, smooth native sliders, and read-back verification.
- Light/dark/Windows default appearance, keyboard support, and Windows UI Automation.
- Isolated helper with timeout recovery; no periodic idle polling, telemetry, or network requests.

Binaries are **unsigned**. **SHA256SUMS** and GitHub build provenance are provided; see [verification instructions](https://github.com/Bauda/steady-brightness/blob/main/docs/install.md#verify-a-download). Monitor/driver/dock compatibility varies. Built-in WMI and NVIDIA fallback paths are implemented but not hardware-validated in the initial test setup.

[Changelog](https://github.com/Bauda/steady-brightness/blob/main/CHANGELOG.md) · [Troubleshooting](https://github.com/Bauda/steady-brightness/blob/main/docs/troubleshooting.md) · [Report a bug](https://github.com/Bauda/steady-brightness/issues/new/choose) · [MIT license](https://github.com/Bauda/steady-brightness/blob/main/LICENSE)
