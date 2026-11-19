$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-app.ps1') -HelpersOnly

function Callback([int]$event) {
    $point=200 -bor (200 -shl 16)
    Assert-True ([BrightnessTestNative]::PostMessage($hostWindow,0x8001,[IntPtr]$point,[IntPtr](0x10000 -bor $event))) 'tray callback posted'
}
function Wait-Condition($condition,[string]$label) {
    $deadline=[DateTime]::UtcNow.AddSeconds(4)
    do {
        if(& $condition) { Assert-True $true $label; return }
        Start-Sleep -Milliseconds 30
    } while([DateTime]::UtcNow -lt $deadline)
    Assert-True $false $label
}
function Menu-Visible {
    $menu=[BrightnessTestNative]::FindOwnWindow($app.Id,'#32768')
    return $menu -ne [IntPtr]::Zero -and [BrightnessTestNative]::IsWindowVisible($menu)
}
function Flyout-Visible { return [BrightnessTestNative]::IsWindowVisible($flyout) }
function Dismiss-Menu {
    [void][BrightnessTestNative]::PostMessage($hostWindow,0x1f,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Condition {-not (Menu-Visible)} 'the tray menu dismisses'
}

try {
    Launch-Test '--demo --background'
    Assert-True (-not (Flyout-Visible)) 'background launch starts with flyout hidden'
    Wait-Condition {
        [int][BrightnessTestNative]::SendMessage($hostWindow,0x8020,[IntPtr]::Zero,[IntPtr]::Zero) -eq 1
    } 'background recovery finishes'
    $scans=[int][BrightnessTestNative]::SendMessage($hostWindow,0x801f,[IntPtr]::Zero,[IntPtr]::Zero)
    Assert-True ($scans -eq 3) 'background launch runs three bounded monitor scans'
    Start-Sleep -Milliseconds 150
    Assert-True ([int][BrightnessTestNative]::SendMessage($hostWindow,0x801f,[IntPtr]::Zero,[IntPtr]::Zero) -eq $scans) 'startup recovery returns to idle'

    Callback 0x007b # WM_CONTEXTMENU from the tray icon.
    Wait-Condition {Menu-Visible} 'right-click opens the options menu'
    Assert-True (-not (Flyout-Visible)) 'right-click from idle does not open the brightness flyout'
    Dismiss-Menu
    Callback 0x0400 # NIN_SELECT
    Wait-Condition {Flyout-Visible} 'left-click still opens brightness controls'
    Callback 0x007b
    Wait-Condition {Menu-Visible} 'right-click opens options while the flyout is open'
    Assert-True (-not (Flyout-Visible)) 'right-click closes an already open brightness flyout'
    Dismiss-Menu
    Callback 0x0401 # NIN_KEYSELECT
    Wait-Condition {Flyout-Visible} 'keyboard selection still opens brightness controls'
    [void][BrightnessTestNative]::PostMessage($flyout,0x0111,[IntPtr]21,[IntPtr]::Zero)
    Wait-Condition {Menu-Visible} 'flyout Options button still opens its menu'
    Assert-True (Flyout-Visible) 'flyout Options button keeps its flyout open'
    Dismiss-Menu
    Close-Test
    $results | Set-Content -LiteralPath (Join-Path $testOutput 'tray-results.txt')
} finally { if($script:app -and -not $script:app.HasExited){Close-Test} }
