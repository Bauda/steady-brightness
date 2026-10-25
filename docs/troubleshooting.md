# Troubleshooting

## Missing or unavailable monitor

1. Wake the monitor and make sure Windows is extending or duplicating the desktop onto it.
2. Enable **DDC/CI** in the monitor's own on-screen menu. Some picture/HDR modes can restrict brightness controls.
3. Press **Refresh**. Sleeping monitors are not permanently marked unsupported.
4. Close other brightness utilities temporarily so only one program sends brightness commands.
5. Try a direct DisplayPort/HDMI connection instead of a dock, KVM, adapter, remote desktop, or virtual display path. Check the monitor and GPU manufacturer's driver guidance.

Detection attempts the Windows low-level DDC/CI brightness command, Windows' high-level monitor API, built-in panel WMI, and an optional NVIDIA I2C fallback as applicable. Detection does not rely solely on advertised monitor capability flags. Nevertheless, some monitors and connection paths do not expose hardware brightness. No software can guarantee support for all of them.

## Brightness does not match the slider

The app reads back changes and reports an unconfirmed write. A monitor may round values, use a restricted range in its current picture mode, or ignore DDC writes. Refresh after adjusting brightness through the monitor's own buttons. There is no continuous polling to track physical-button changes while idle.

## Brightness jumps to 100% after powering on

Version 1.0.3 remembers the chosen hardware brightness for each external monitor. Leave **Options > Restore brightness after wake** enabled, then set your preferred brightness through its slider once. Confirmed changes survive app restarts. Reconnect/wake notifications trigger three bounded recovery passes, even while the flyout is hidden, to allow slow monitor firmware to settle.

If Windows receives no reconnect or wake event because the monitor keeps its display connection alive, open the flyout or press Refresh to restore the saved value. Continuous polling is intentionally avoided. Disable the restore option when you want brightness changes made through the monitor's on-screen menu to become the current baseline. Device identity must be unambiguous; cloned/multiple physical panels behind one logical source may be excluded.

## Slow or stuck monitor command

The tray uses a separate helper and stops an operation after approximately nine seconds. Wait for the status, then press **Refresh** to reconnect. Pending slider movements are coalesced to reduce commands. A slow monitor can still take time to apply and confirm a value.

## Icon hidden or missing

Check the **^** tray overflow area. Windows controls which tray icons are visible; drag the sun into the visible area or use **Options > Keep tray icon visible** to open taskbar settings. Launching Steady Brightness again reopens the existing flyout. The app re-registers its icon when Explorer recreates the taskbar.

## Diagnostics

The following command reads monitor information and writes a local report; it does not intentionally change brightness:

```powershell
Start-Process .\SteadyBrightness.exe -ArgumentList '--probe', '"monitor-report.txt"' -Wait
```

Wake monitors first. Unlike the regular tray UI, this standalone diagnostic executes monitor API calls directly and has no helper timeout; stop it in Task Manager if a driver hangs. Reports may contain monitor identifiers: review and redact them before sharing. Nothing is uploaded automatically.

For a report, include the app version, Windows version, monitor model, GPU/driver, cable/dock path, DDC/CI setting, and exact status message in the [bug form](https://github.com/Bauda/steady-brightness/issues/new/choose). Avoid serial numbers, private paths, and full system dumps. Security reports follow [SECURITY.md](../SECURITY.md).
