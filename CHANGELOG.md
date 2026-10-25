# Changelog

## 1.0.3

- Remember confirmed brightness for each external monitor and restore it after power-on, reconnect, or system resume, including while the flyout is hidden.
- Match device interface identities rather than Windows display numbers or model names. Skip ambiguous identities and built-in panels managed by Windows power policy.
- Use three bounded recovery passes to handle slow wake-up and late firmware resets, then release the helper and return to idle. No continuous polling.
- Add **Options > Restore brightness after wake** (on by default), with a persisted opt-out.
- Add isolated persistence and simulated power-cycle regression tests, including reordered displays and a Samsung reset to 100%.

## 1.0.2

- First public open-source release with an MIT license, Windows build/test automation, and packaged downloads.
- Smooth native sliders with consistent drawing, direct track clicks, keyboard controls, and Windows UI Automation support.
- Compact flyout with an independent slider per monitor; removed the redundant heading and subtitle.
- Light, dark, or Windows default theme, with high-contrast support.
- DDC/CI controls, read-back verification, WMI and optional NVIDIA fallbacks, and isolated worker crash/timeout handling.
- Per-user installation, optional startup, portable use, and explicit uninstallation.

## Earlier local versions

Versions 1.0.0 and 1.0.1 were local builds used to develop monitor detection, brightness verification, theme support, and the first slider fixes. They were not published as GitHub releases.
