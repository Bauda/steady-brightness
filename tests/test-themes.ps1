$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-app.ps1') -HelpersOnly
$results=[Collections.Generic.List[string]]::new()
$key='HKCU:\Software\SteadyBrightness'
$hadKey=Test-Path -LiteralPath $key
$saved=if($hadKey){Get-ItemProperty -LiteralPath $key -Name Theme -ErrorAction SilentlyContinue}else{$null}
function Capture($name) {
    $rect=[BrightnessTestNative+Rect]::new();[void][BrightnessTestNative]::GetWindowRect($flyout,[ref]$rect)
    $bitmap=[Drawing.Bitmap]::new(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top))
    $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
    [void][BrightnessTestNative]::PrintWindow($flyout,$dc,2);$graphics.ReleaseHdc($dc)
    $bitmap.Save((Join-Path $testOutput $name),[Drawing.Imaging.ImageFormat]::Png)
    $pixel=$bitmap.GetPixel(5,5)
    $graphics.Dispose();$bitmap.Dispose()
    return $pixel
}
function Choose($id) {
    [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]$id,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}
try {
    Launch-Test '--demo'
    Choose 11
    $pixel=Capture 'theme-light.png'
    Assert-True ($pixel.R -gt 240 -and $pixel.G -gt 240) 'light theme renders a light background'
    Assert-True ((Get-ItemPropertyValue -LiteralPath $key -Name Theme) -eq 1) 'light preference saved'
    Choose 12
    $pixel=Capture 'theme-dark.png'
    Assert-True ($pixel.R -lt 40 -and $pixel.G -lt 40) 'dark theme renders a dark background'
    Assert-True ((Get-ItemPropertyValue -LiteralPath $key -Name Theme) -eq 2) 'dark preference saved'
    Close-Test
    Launch-Test '--demo'
    $pixel=Capture 'theme-restarted.png'
    Assert-True ($pixel.R -lt 40) 'theme preference survives restart'
    Choose 10
    $pixel=Capture 'theme-system.png'
    $osLight=1; $osTheme=Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -Name AppsUseLightTheme -ErrorAction SilentlyContinue; if($null -ne $osTheme){$osLight=$osTheme.AppsUseLightTheme}
    Assert-True (($osLight -eq 0 -and $pixel.R -lt 40) -or ($osLight -ne 0 -and $pixel.R -gt 240)) 'Use Windows setting matches current PC theme'
    $gdiBefore=[BrightnessTestNative]::GetGuiResources($app.Handle,0)
    for($i=0;$i -lt 50;$i++) { Choose (11+($i%2)) }
    $gdiAfter=[BrightnessTestNative]::GetGuiResources($app.Handle,0)
    Assert-True ($gdiAfter -le $gdiBefore+1) "50 theme switches do not leak GDI objects ($gdiBefore -> $gdiAfter)"
    Choose 10
    [void][BrightnessTestNative]::SendMessage($hostWindow,0x1a,[IntPtr]::Zero,[IntPtr]::Zero)
    $pixel=Capture 'theme-system-event.png'
    Assert-True (($osLight -eq 0 -and $pixel.R -lt 40) -or ($osLight -ne 0 -and $pixel.R -gt 240)) 'Windows settings event refreshes automatic theme'
    Close-Test
    $results|Set-Content -LiteralPath (Join-Path $testOutput 'theme-results.txt')
} finally {
    if($script:app -and -not $script:app.HasExited){Close-Test}
    if($saved -and $null -ne $saved.Theme){Set-ItemProperty -LiteralPath $key -Name Theme -Type DWord -Value $saved.Theme}
    elseif($hadKey){Remove-ItemProperty -LiteralPath $key -Name Theme -ErrorAction SilentlyContinue}
    elseif(Test-Path -LiteralPath $key){Remove-Item -LiteralPath $key}
}
