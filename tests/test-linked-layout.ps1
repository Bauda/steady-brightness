$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-slider-input.ps1') -OnlyInputHelpers
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class LinkedLayoutNative {
 [StructLayout(LayoutKind.Sequential)]public struct Size{public int Width,Height;}
 [DllImport("gdi32.dll")]public static extern IntPtr SelectObject(IntPtr dc,IntPtr item);
 [DllImport("gdi32.dll",CharSet=CharSet.Unicode)]public static extern bool GetTextExtentPoint32(IntPtr dc,string text,int count,out Size size);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int X,Y;}
}
'@
$originalCursor=[LinkedLayoutNative+Point]::new();[void][LinkedLayoutNative]::GetCursorPos([ref]$originalCursor)
function Controls { return @(Child-Controls $flyout) }
function Button { return Controls | Where-Object Id -eq 22 }
function Choose([int]$id) { [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]$id,[IntPtr]::Zero);Start-Sleep -Milliseconds 80 }
function Bounds($control) {
    $rect=[BrightnessTestNative+Rect]::new()
    [void][BrightnessTestNative]::GetWindowRect($control.Handle,[ref]$rect)
    return $rect
}
function Click-Link {
    $button=Button
    $rect=Bounds $button
    $x=[int](($rect.Right-$rect.Left)/2);$y=[int](($rect.Bottom-$rect.Top)/2)
    [void][LinkedLayoutNative]::SetCursorPos($rect.Left+$x,$rect.Top+$y)
    Mouse $button 0x201 $x $y 1
    Mouse $button 0x202 $x $y 0
    Start-Sleep -Milliseconds 120
}
function Check-FlyoutPaint([string]$name) {
    Start-Sleep -Milliseconds 80
    $control=[pscustomobject]@{Handle=$flyout}
    $live=Capture-Client $control
    [void][SliderInputNative]::RedrawWindow($flyout,[IntPtr]::Zero,[IntPtr]::Zero,0x585)
    $fresh=Capture-Client $control
    try {
        $live.Save((Join-Path $testOutput ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
        $different=Difference $live $fresh
        if($different){$fresh.Save((Join-Path $testOutput ($name+'-fresh.png')),[Drawing.Imaging.ImageFormat]::Png)}
        Assert-True ($different -eq 0) "$name first-click painting matches full repaint ($different differences)"
    } finally {$live.Dispose();$fresh.Dispose()}
}
function Check-Padding {
    $button=Button;$rect=Bounds $button
    $font=[BrightnessTestNative]::SendMessage($button.Handle,0x31,[IntPtr]::Zero,[IntPtr]::Zero)
    $dc=[SliderInputNative]::GetDC($button.Handle);$old=[LinkedLayoutNative]::SelectObject($dc,$font)
    try {
        $size=[LinkedLayoutNative+Size]::new()
        [void][LinkedLayoutNative]::GetTextExtentPoint32($dc,$button.Text,$button.Text.Length,[ref]$size)
        Assert-True (($rect.Right-$rect.Left-$size.Width) -ge 24) 'link button reserves at least 12px horizontal padding on each side'
        Assert-True (($rect.Bottom-$rect.Top-$size.Height) -ge 12) 'link button has comfortable vertical padding'
    } finally {[void][LinkedLayoutNative]::SelectObject($dc,$old);[void][SliderInputNative]::ReleaseDC($button.Handle,$dc)}
    $buttons=@(Controls | Where-Object { $_.Class -eq 'Button' -and $_.Visible } | Sort-Object Id)
    $refresh=Bounds $buttons[0];$options=Bounds $buttons[1];$link=Bounds $buttons[2]
    Assert-True ($refresh.Right+8 -le $link.Left -and $link.Right+8 -le $options.Left) 'footer buttons retain gaps without overlap'
}
function Check-MouseFocusPaint {
    $bitmap=Capture-Client (Button)
    try {
        # Above the caption and inside the rounded border, this band should be
        # solid button background, not the old black/white XOR focus rectangle.
        $reference=$bitmap.GetPixel(10,4).ToArgb();$different=0
        for($x=10;$x -lt $bitmap.Width-10;$x++) {
            if($bitmap.GetPixel($x,4).ToArgb() -ne $reference){$different++}
        }
        Assert-True ($different -eq 0) 'mouse-clicked button has no dotted inner focus artifact'
    } finally {$bitmap.Dispose()}
}
function Notice-Visible { return @(Controls | Where-Object { $_.Text -like 'Changes are checked*' -and $_.Visible }).Count -gt 0 }
try {
    Launch-Test '--demo'
    [void][SliderInputNative]::SetForegroundWindow($flyout)
    Click-Link
    Check-FlyoutPaint 'linked-first-click'
    Check-MouseFocusPaint
    Assert-True (-not (Notice-Visible)) 'normal confirmation line is hidden in linked mode'
    $compact=Bounds ([pscustomobject]@{Handle=$flyout})
    Check-Padding
    [void][BrightnessTestNative]::SendMessage($hostWindow,0x8023,[IntPtr]1,[IntPtr]::Zero)
    $errorControl=Controls | Where-Object { $_.Text -like 'Monitor helper stopped*' -and $_.Visible }
    Assert-True ($null -ne $errorControl) 'linked mode still shows helper errors'
    $expanded=Bounds ([pscustomobject]@{Handle=$flyout});$errorRect=Bounds $errorControl;$buttonRect=Bounds (Button)
    Assert-True (($expanded.Bottom-$expanded.Top) -gt ($compact.Bottom-$compact.Top)) 'linked mode makes room for an error message'
    Assert-True ($errorRect.Bottom -le $buttonRect.Top) 'linked error does not overlap footer buttons'
    Check-FlyoutPaint 'linked-error'
    Choose 20
    Assert-True (-not (Notice-Visible)) 'successful refresh keeps the normal linked confirmation hidden'
    $recovered=Bounds ([pscustomobject]@{Handle=$flyout})
    Assert-True (($recovered.Bottom-$recovered.Top) -eq ($compact.Bottom-$compact.Top)) 'linked mode compacts again after error recovery'
    Click-Link
    Check-FlyoutPaint 'unlinked-first-click'
    Check-MouseFocusPaint
    Assert-True (Notice-Visible) 'unlinked mode keeps the normal confirmation line'
    Check-Padding
    foreach($theme in @(11,12)) {
        Choose $theme
        foreach($scale in @(120,144,192,96)) {
            [void][BrightnessTestNative]::SendMessage($flyout,0x2e0,[IntPtr]($scale -bor ($scale -shl 16)),[IntPtr]::Zero)
            Start-Sleep -Milliseconds 100
            Click-Link
            Check-FlyoutPaint "linked-theme-$theme-dpi-$scale"
            Check-Padding
            Click-Link
            Check-FlyoutPaint "unlinked-theme-$theme-dpi-$scale"
            Assert-True (Notice-Visible) 'nonlinked confirmation survives theme and DPI relayout'
        }
    }
    Close-Test
    $results | Set-Content -LiteralPath (Join-Path $testOutput 'linked-layout-results.txt')
} finally {
    if($script:app -and -not $script:app.HasExited){Close-Test}
    [void][LinkedLayoutNative]::SetCursorPos($originalCursor.X,$originalCursor.Y)
    if($saved -and $null -ne $saved.Theme){Set-ItemProperty -LiteralPath $key -Name Theme -Type DWord -Value $saved.Theme}
    else {Remove-ItemProperty -LiteralPath $key -Name Theme -ErrorAction SilentlyContinue}
}
