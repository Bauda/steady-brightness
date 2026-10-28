Native Windows monitor brightness control with independent sliders or one linked slider for all displays.

### New in v1.0.4

- **Link monitors** adopts the primary monitor's brightness, then shows one **All monitors** slider. **Unlink monitors** restores individual controls at their current levels. The button is hidden with one display.
- Linked mode and its shared target survive app restarts. Reconnected displays join that level through the existing bounded recovery sequence; unavailable or failed monitors remain visible as errors.
- Fix first-click rendering and the dotted button-focus artifact. Buttons have more caption padding and retain a clean keyboard-focus outline.
- Compact the linked layout by removing the redundant verification footer. Errors still receive space, and the footer remains in independent mode.
- Keep event-driven operation with coalesced changes and no continuous monitor polling.

Validation includes Windows build/tests, accessibility, native button painting, light/dark themes, simulated DPI layouts, persistence, and simulated monitor reconnect/failure cases. Physical compatibility depends on the monitor and connection.

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
