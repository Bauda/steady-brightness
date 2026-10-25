$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-app.ps1') -HelpersOnly
function Send-Host([int]$message,[int]$value=0) {
    return [int][BrightnessTestNative]::SendMessage($hostWindow,$message,[IntPtr]$value,[IntPtr]::Zero)
}
function Wait-Recovery {
    $deadline=[DateTime]::UtcNow.AddSeconds(4)
    do {Start-Sleep -Milliseconds 40;$done=Send-Host 0x8020} while(-not $done -and [DateTime]::UtcNow -lt $deadline)
    Assert-True ($done -eq 1) 'reconnect work ends after a bounded recovery burst'
}
function Sliders {
    return @(Child-Controls $flyout | Where-Object Class -eq 'SteadyBrightness.Slider.1' | Sort-Object Id)
}
function Set-Level($control,[int]$percent) {
    [void][BrightnessTestNative]::SendMessage($control.Handle,0x405,[IntPtr]1,[IntPtr]$percent)
    [void][BrightnessTestNative]::SendMessage($flyout,0x114,[IntPtr]8,$control.Handle)
}
function Get-Level($control) {
    return [int][BrightnessTestNative]::SendMessage($control.Handle,0x400,[IntPtr]::Zero,[IntPtr]::Zero)
}
try {
    Launch-Test '--demo'
    $first=Sliders
    Set-Level $first[0] 28
    Set-Level $first[1] 37
    [void][BrightnessTestNative]::SendMessage($flyout,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    $before=Send-Host 0x801f
    [void](Send-Host 0x801e 3) # Samsung at 100%, reordered displays, unavailable on first scan.
    Wait-Recovery
    $now=Sliders
    Assert-True ((Get-Level $now[0]) -eq 37) 'Samsung returns to its confirmed 37% after reset and late availability'
    Assert-True ((Get-Level $now[1]) -eq 28) 'Acer keeps its own value when Windows display numbers change'
    Assert-True (-not [BrightnessTestNative]::IsWindowVisible($flyout)) 'wake recovery runs with flyout hidden'
    $after=Send-Host 0x801f
    Assert-True (($after-$before) -eq 3) 'exactly three recovery scans run'
    Start-Sleep -Milliseconds 250
    Assert-True ((Send-Host 0x801f) -eq $after) 'no continuing scan timer after recovery'

    Set-Level $now[0] 64
    [void](Send-Host 0x801e 5) # Reordered, plus firmware resets again during the second pass.
    Wait-Recovery
    $now=Sliders
    Assert-True ((Get-Level $now[0]) -eq 64) 'latest user selection replaces the old saved value'
    Assert-True ((Get-Level $now[1]) -eq 28) 'late firmware reset recovery leaves the other monitor unchanged'
    Set-Level $now[0] 0
    [void](Send-Host 0x801e 1)
    Wait-Recovery
    $now=Sliders
    Assert-True ((Get-Level $now[0]) -eq 0) 'zero brightness is a valid saved target'
    [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]13,[IntPtr]::Zero)
    $before=Send-Host 0x801f
    [void](Send-Host 0x801e 1)
    Start-Sleep -Milliseconds 150
    Assert-True ((Send-Host 0x801f) -eq $before) 'disabling wake restoration also stops background recovery'
    [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]20,[IntPtr]::Zero)
    $now=Sliders
    Assert-True ((Get-Level $now[0]) -eq 100) 'disabled restoration accepts the actual hardware value'
    [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]13,[IntPtr]::Zero)
    $now=Sliders;Set-Level $now[0] 42
    [void](Send-Host 0x801e 1)
    Wait-Recovery
    $now=Sliders
    Assert-True ((Get-Level $now[0]) -eq 42) 're-enabling restoration remembers new selections'
    [void](Send-Host 0x801e 1)
    [void][BrightnessTestNative]::SendMessage($now[0].Handle,0x405,[IntPtr]1,[IntPtr]17)
    [void][BrightnessTestNative]::SendMessage($flyout,0x114,[IntPtr]5,$now[0].Handle)
    Wait-Recovery
    $now=Sliders
    Assert-True ((Get-Level $now[0]) -eq 17) 'a newly queued manual change wins over wake recovery'
    [void](Send-Host 0x218 18) # PBT_APMRESUMEAUTOMATIC, still simulated monitors only.
    Wait-Recovery
    Assert-True (-not $app.HasExited) 'system resume also recovers without quitting'
    Close-Test
    $results | Set-Content -LiteralPath (Join-Path $testOutput 'reconnect-results.txt')
} finally {if($script:app -and -not $script:app.HasExited){Close-Test}}
