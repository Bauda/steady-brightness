$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-app.ps1') -HelpersOnly
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName UIAutomationClient
$themeKey='HKCU:\Software\SteadyBrightness'
$originalTheme=Get-ItemProperty -LiteralPath $themeKey -Name Theme -ErrorAction SilentlyContinue
function Send-Host([int]$message,[int]$value=0) {
    return [int][BrightnessTestNative]::SendMessage($hostWindow,$message,[IntPtr]$value,[IntPtr]::Zero)
}
function Controls { return @(Child-Controls $flyout) }
function Physical { return @(Controls | Where-Object { $_.Class -eq 'SteadyBrightness.Slider.1' -and $_.Id -ge 100 } | Sort-Object Id) }
function Visible-Sliders { return @(Controls | Where-Object { $_.Class -eq 'SteadyBrightness.Slider.1' -and $_.Visible }) }
function Shared-Slider { return Controls | Where-Object Id -eq 99 }
function Set-Level($control,[int]$value,[int]$notification=8) {
    [void][BrightnessTestNative]::SendMessage($control.Handle,0x405,[IntPtr]1,[IntPtr]$value)
    [void][BrightnessTestNative]::SendMessage($flyout,0x114,[IntPtr]$notification,$control.Handle)
}
function Choose([int]$id) { [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]$id,[IntPtr]::Zero) }
function Levels([int]$first,[int]$second) {
    return (Send-Host 0x8022 0) -eq $first -and (Send-Host 0x8022 1) -eq $second
}
function Capture([string]$name) {
    if(-not [BrightnessTestNative]::IsWindowVisible($flyout)){[void](Send-Host 0x8002)}
    Start-Sleep -Milliseconds 150
    $rect=[BrightnessTestNative+Rect]::new();[void][BrightnessTestNative]::GetWindowRect($flyout,[ref]$rect)
    $bitmap=[Drawing.Bitmap]::new(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top))
    $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
    try { [void][BrightnessTestNative]::PrintWindow($flyout,$dc,2) }
    finally { $graphics.ReleaseHdc($dc);$graphics.Dispose() }
    try {
        $bitmap.Save((Join-Path $testOutput $name),[Drawing.Imaging.ImageFormat]::Png)
        Assert-True ($bitmap.GetPixel(5,5).R -gt 0) "$name captures the rendered flyout"
    }
    finally { $bitmap.Dispose() }
}
function Wait-Recovery {
    $deadline=[DateTime]::UtcNow.AddSeconds(4)
    do { Start-Sleep -Milliseconds 40;$done=Send-Host 0x8020 } while(-not $done -and [DateTime]::UtcNow -lt $deadline)
    Assert-True ($done -eq 1) 'linked recovery finishes its bounded burst'
}
try {
    Launch-Test '--demo'
    $physical=Physical
    Set-Level $physical[0] 0;Set-Level $physical[1] 60
    $rect=[BrightnessTestNative+Rect]::new();[void][BrightnessTestNative]::GetWindowRect($flyout,[ref]$rect)
    $unlinkedHeight=$rect.Bottom-$rect.Top
    Choose 22
    Assert-True (Levels 0 0) 'linking adopts the primary monitor including 0%'
    $visible=@(Visible-Sliders)
    Assert-True ($visible.Count -eq 1 -and $visible[0].Id -eq 99) 'linked mode shows only one unified slider'
    Assert-True (@(Controls | Where-Object { $_.Id -eq 22 -and $_.Text -eq 'Unlink monitors' -and $_.Visible }).Count -eq 1) 'linked mode exposes the unlink action'
    [void][BrightnessTestNative]::GetWindowRect($flyout,[ref]$rect)
    Assert-True (($rect.Bottom-$rect.Top) -lt $unlinkedHeight) 'linked mode compacts the flyout'
    $element=[System.Windows.Automation.AutomationElement]::FromHandle((Shared-Slider).Handle)
    Assert-True ($element.Current.Name -eq 'All monitors') 'unified slider has an accessible group name'
    $range=[System.Windows.Automation.RangeValuePattern]$element.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern)
    $range.SetValue(30)
    Assert-True (Levels 30 30) 'accessible unified slider applies 30% to both monitors'
    Assert-True ($range.Current.Value -eq 30) 'unified slider reports its accessible value'
    foreach($level in 1..40) { Set-Level (Shared-Slider) $level 5 }
    Set-Level (Shared-Slider) 70
    Assert-True (Levels 70 70) 'rapid linked changes settle on the final target'
    $preference=Get-ItemPropertyValue -LiteralPath "HKCU:\Software\SteadyBrightness.Tests\$($app.Id)" -Name LinkedBrightness
    Assert-True ($preference -eq 71) 'linked mode and target persist together'
    Capture 'linked-system.png'
    Choose 12;Capture 'linked-dark.png'
    Choose 11;Capture 'linked-light.png'
    Choose 10

    [void][BrightnessTestNative]::SendMessage($flyout,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    $before=Send-Host 0x801f
    [void](Send-Host 0x801e 3)
    Wait-Recovery
    Assert-True (Levels 70 70) 'hidden linked mode survives a reset, reordered displays and late availability'
    Assert-True (((Send-Host 0x801f)-$before) -eq 3) 'linked recovery still uses exactly three scans'
    Start-Sleep -Milliseconds 200
    Assert-True (((Send-Host 0x801f)-$before) -eq 3) 'linked mode adds no continuous recovery polling'
    [void](Send-Host 0x8002)
    # Matching linked read-back must replace a stale individual wake preference.
    Set-ItemProperty -LiteralPath "HKCU:\Software\SteadyBrightness.Tests\$($app.Id)" -Name 'Brightness:demo:acer' -Type DWord -Value 2
    Choose 20
    Choose 22
    $visible=@(Visible-Sliders)
    Assert-True ($visible.Count -eq 2 -and @($visible | Where-Object Id -eq 99).Count -eq 0) 'unlink restores individual controls'
    Assert-True (Levels 70 70) 'unlinking leaves current brightness unchanged'
    Choose 20
    Assert-True (Levels 70 70) 'unlink then refresh preserves confirmed linked levels instead of restoring stale individual preferences'
    $physical=Physical # Samsung is first after reordering, Acer remains primary.
    Set-Level $physical[0] 80;Set-Level $physical[1] 10
    Assert-True (Levels 10 80) 'unlinked sliders remain independent'
    Choose 22
    Assert-True (Levels 10 10) 'relinking uses the primary monitor instead of the first row'
    [void](Send-Host 0x801e 0);Wait-Recovery # Put Acer first again.
    Choose 22
    [void](Send-Host 0x8021 2) # Samsung is now primary.
    $physical=Physical;Set-Level $physical[0] 20;Set-Level $physical[1] 65
    Choose 22
    Assert-True (Levels 65 65) 'changing the Windows primary display changes the initial linked source'

    [void](Send-Host 0x8021 4) # Simulate a failed Samsung write.
    Set-Level (Shared-Slider) 90
    Assert-True (Levels 90 65) 'one failed monitor does not stop the other monitor'
    Assert-True (@(Controls | Where-Object { $_.Text -like 'Some monitors did not sync*' -and $_.Visible }).Count -eq 1) 'partial failure stays visible on unified slider'
    [void](Send-Host 0x8021 0)
    Assert-True (Levels 90 90) 'refresh retries the linked target after a monitor recovers'
    [void](Send-Host 0x8021 8) # Samsung unavailable.
    Set-Level (Shared-Slider) 45
    Assert-True (Levels 45 90) 'unavailable monitor does not block linked control of available monitors'
    Assert-True ((Shared-Slider).Enabled) 'unified slider stays usable while one monitor is unavailable'
    [void](Send-Host 0x8021 0)
    Assert-True (Levels 45 45) 'returning monitor joins the saved linked target'
    [void](Send-Host 0x8021 1) # Only Acer connected, with linking still enabled.
    Assert-True (@(Visible-Sliders).Count -eq 1 -and (Visible-Sliders)[0].Id -eq 100) 'one-monitor setup keeps the original slider'
    Assert-True (@(Controls | Where-Object { $_.Id -eq 22 -and $_.Visible }).Count -eq 0) 'link controls are hidden for one monitor'
    Set-Level (Physical)[0] 36
    [void](Send-Host 0x8021 0)
    Assert-True (Levels 36 36) 'second monitor rejoins the latest single-monitor level'
    Assert-True (@(Visible-Sliders).Count -eq 1 -and (Visible-Sliders)[0].Id -eq 99) 'two-monitor return restores the unified view'
    Choose 22
    [void](Send-Host 0x8021 10) # Unavailable Samsung is the primary.
    Assert-True (-not (Controls | Where-Object Id -eq 22).Enabled) 'linking waits for the actual primary instead of guessing another source'
    [void](Send-Host 0x8021 0)
    $gdiBefore=[BrightnessTestNative]::GetGuiResources($app.Handle,0)
    1..100 | ForEach-Object { Choose 22 }
    $gdiAfter=[BrightnessTestNative]::GetGuiResources($app.Handle,0)
    Assert-True ($gdiAfter -le $gdiBefore+1) "repeated link toggles do not leak GDI objects ($gdiBefore -> $gdiAfter)"
    $preference=Get-ItemPropertyValue -LiteralPath "HKCU:\Software\SteadyBrightness.Tests\$($app.Id)" -Name LinkedBrightness
    Assert-True ($preference -eq 0) 'unlinked mode persists'
    [void](Send-Host 0x8021 1)
    Assert-True (@(Visible-Sliders).Count -eq 1 -and (Visible-Sliders)[0].Id -eq 100) 'unlinked one-monitor setup retains its individual slider'
    Assert-True (@(Controls | Where-Object { $_.Id -eq 22 -and $_.Visible }).Count -eq 0) 'unlinked one-monitor setup also hides link controls'
    Close-Test
    $results | Set-Content -LiteralPath (Join-Path $testOutput 'linked-results.txt')
} finally {
    if($script:app -and -not $script:app.HasExited){Close-Test}
    if($null -ne $originalTheme -and $null -ne $originalTheme.Theme){Set-ItemProperty -LiteralPath $themeKey -Name Theme -Type DWord -Value $originalTheme.Theme}
    else {Remove-ItemProperty -LiteralPath $themeKey -Name Theme -ErrorAction SilentlyContinue}
}
