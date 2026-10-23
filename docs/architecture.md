# Architecture

## Resident UI

`main.cpp` owns a native Win32 message loop, notification icon, compact flyout, theme settings, installation, and command scheduling. The executable statically links its C++ runtime. There is no web renderer, service, network client, updater, or continuous idle timer. Display, power, theme, and taskbar messages drive relevant updates.

The UI coalesces slider changes for approximately 130 ms and promptly sends the final value on release. Each row maintains its own desired value. Read-back completion does not pull a held thumb away from the pointer.

## Monitor helper

The same executable runs in `--worker` mode with explicitly inherited mapping/event handles and a parent-process handle. `common.h` defines the bounded shared-memory protocol (up to 32 entries). A job object ties helper lifetime to the parent. The UI supervises commands with an approximately nine-second timeout, kills a hung helper, and offers recovery through Refresh. Hiding the flyout ends the helper after pending writes finish.

`monitor.cpp` enumerates physical displays and built-in panels, reads brightness, scales device ranges to percentages, and verifies writes with bounded retries. Backends include VCP 0x10 via Windows DDC/CI, the Windows high-level monitor API, WMI for compatible built-in panels, and optional NVIDIA direct I2C. `nv-direct.h` loads the installed system NVIDIA library only when needed; driver binaries are not redistributed.

## Slider

`slider.cpp` implements one native window per slider. It draws rounded, antialiased tracks and thumbs into a temporary DIB on repaint and then releases it. Mouse, keyboard, disabled/focus states, and UI Automation range/name/value use the same control. UIAutomationCore is delay-loaded for accessibility. There is no retained frame loop or extra graphics runtime.

## Files and settings

- `HKCU\Software\SteadyBrightness\Theme`: 0 = Windows default, 1 = light, 2 = dark. High contrast takes priority.
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SteadyBrightness`: optional startup command.
- `%LOCALAPPDATA%\Programs\SteadyBrightness`: per-user installation and embedded removal scripts.
- Per-user Start menu shortcut and Windows uninstall registration.

Uninstallation removes known application files without recursively deleting user-added content. Ordinary operation creates no log files. `--probe <path>` creates a report only when explicitly requested.

## Entry points for contributors

`--demo` substitutes two simulated monitors and suppresses the normal tray icon. `--test-crash` and `--test-timeout` exercise helper failures without monitor writes; do not press Refresh in those modes unless you intend to probe actual monitors. `--test-hardware <path>` is a developer diagnostic that **writes brightness and attempts restoration**; it is not part of CI. See [testing](testing.md) before hardware validation.

## Upstream references

- [Windows DDC brightness reads](https://learn.microsoft.com/en-us/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-getvcpfeatureandvcpfeaturereply) and [writes](https://learn.microsoft.com/en-us/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-setvcpfeature).
- [Windows WMI brightness](https://learn.microsoft.com/en-us/windows/win32/wmicoreprov/wmisetbrightness-method-in-class-wmimonitorbrightnessmethods).
- [NVIDIA public NVAPI headers](https://github.com/NVIDIA/nvapi).
