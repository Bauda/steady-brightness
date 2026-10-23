# Installation and updates

## Requirements

- Windows 10 or 11 on x64.
- External monitor with hardware brightness control over DDC/CI, or a built-in panel exposing Windows WMI brightness.
- For external monitors, DDC/CI enabled in the monitor's on-screen menu. Docks and adapters may block it.

No administrator access or separate C++/.NET runtime installation is needed. Only x64 is currently built and tested.

## Install

Download `SteadyBrightness-Windows-x64.zip` from the [latest release](https://github.com/Bauda/steady-brightness/releases/latest), extract the whole archive, and run `install.cmd`. It invokes the included executable's `--install` mode.

Installation copies the app and its embedded uninstall scripts into `%LOCALAPPDATA%\Programs\SteadyBrightness`, creates a Start menu shortcut, and registers a per-user Windows uninstall entry. It does not automatically enable startup. Choose **Options > Start with Windows** to opt in.

Click the sun beside the clock to show the flyout. If hidden under **^**, drag it into the visible tray area. **Keep tray icon visible** opens Windows taskbar settings; Windows owns the visibility preference. You may also pin the installed Start menu shortcut to the taskbar. Opening another copy toggles the existing flyout.

Choose **Options > Theme: Use Windows setting**, **Light**, or **Dark**. Escape or clicking elsewhere hides the flyout. Arrow keys step by 1%; Page keys step by 10%; Home/End select 0/100%.

## Verify a download

The release includes `SHA256SUMS`, `SteadyBrightness.exe`, and the ZIP. In PowerShell, after downloading the ZIP and checksum file into the same folder:

```powershell
Get-FileHash .\SteadyBrightness-Windows-x64.zip -Algorithm SHA256
Get-Content .\SHA256SUMS
```

Compare the calculated hash with the ZIP's line in `SHA256SUMS`. The executable inside the archive must match the executable's checksum too. Fetch files from this repository's official release, not a third-party reupload.

With a current [GitHub CLI](https://cli.github.com/), you can additionally verify the ZIP's GitHub build provenance:

```powershell
gh attestation verify .\SteadyBrightness-Windows-x64.zip --repo Bauda/steady-brightness
```

The executable is currently unsigned. Windows SmartScreen may warn about the unknown publisher. Review the source or build it yourself if you cannot establish trust in a download. Do not turn off security globally.

## Update

Choose **Options > Exit**, download and extract the new release, and run its `install.cmd`. Theme and startup preferences are retained. There is no automatic updater and the running app does not check the internet for releases. GitHub's **Watch > Custom > Releases** can notify you of releases if you choose.

## Portable use

Run `SteadyBrightness.exe` directly without installing. If you enable **Start with Windows**, keep that executable in a permanent location; the startup entry points to it. To remove a portable copy, turn startup off, exit the app, and delete the executable. Theme preferences remain in `HKCU\Software\SteadyBrightness` unless you remove them yourself.

## Uninstall

Use **Windows Settings > Apps > Installed apps > Steady Brightness > Uninstall**, or run `%LOCALAPPDATA%\Programs\SteadyBrightness\Uninstall.cmd`.

The uninstaller stops the installed copy and removes its known files, Start shortcut, startup entry, uninstall registration, and theme preference. It leaves unrelated files in the installation directory. Remove any independently pinned taskbar shortcut manually.
