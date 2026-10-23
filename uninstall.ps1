$ErrorActionPreference = 'Stop'
try {
    $expected = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\SteadyBrightness')).TrimEnd('\')
    $actual = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
    if (-not [string]::Equals($actual, $expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Run the uninstaller from the installed SteadyBrightness folder.'
    }
    $exe = Join-Path $expected 'SteadyBrightness.exe'
    Get-Process -Name SteadyBrightness -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and [string]::Equals([IO.Path]::GetFullPath($_.Path), $exe, [StringComparison]::OrdinalIgnoreCase)
    } | Stop-Process -Force
    $startupKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    Remove-ItemProperty -LiteralPath $startupKey -Name SteadyBrightness -ErrorAction SilentlyContinue
    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SteadyBrightness'
    if (Test-Path -LiteralPath $uninstallKey) { Remove-Item -LiteralPath $uninstallKey }
    $settingsKey = 'HKCU:\Software\SteadyBrightness'
    if (Test-Path -LiteralPath $settingsKey) { Remove-Item -LiteralPath $settingsKey }
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Steady Brightness.lnk'
    if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
    foreach ($name in @('SteadyBrightness.exe', 'Uninstall.cmd', 'Uninstall.ps1')) {
        $file = [IO.Path]::GetFullPath((Join-Path $expected $name))
        if ([IO.Path]::GetDirectoryName($file) -ne $expected) { throw 'Unexpected uninstall path.' }
        for ($attempt = 0; (Test-Path -LiteralPath $file) -and $attempt -lt 10; $attempt++) {
            try { Remove-Item -LiteralPath $file -Force } catch { if ($attempt -eq 9) { throw }; Start-Sleep -Milliseconds 300 }
        }
    }
    # No recursive delete: leave any unrelated files the user added to the folder.
    if (-not (Get-ChildItem -LiteralPath $expected -Force)) { Remove-Item -LiteralPath $expected }
    Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.MessageBox]::Show('Steady Brightness has been removed.', 'Steady Brightness')
} catch {
    Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Could not uninstall Steady Brightness')
    exit 1
}
