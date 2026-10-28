param([switch]$HelpersOnly)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class BrightnessTestNative {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc,IntPtr param);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 public static IntPtr FindOwnWindow(uint pid,string cls) {
  IntPtr found=IntPtr.Zero;
  EnumWindows(delegate(IntPtr h,IntPtr unused) {
   uint owner;GetWindowThreadProcessId(h,out owner);
   if(owner==pid) {var name=new StringBuilder(128);GetClassName(h,name,128);if(name.ToString()==cls){found=h;return false;}}
   return true;
  },IntPtr.Zero);
  return found;
 }
 public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd,StringBuilder text,int max);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,StringBuilder text,int max);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hwnd,EnumProc proc,IntPtr param);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string value);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Rect rect);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process,uint flags);
}
'@
Add-Type -AssemblyName System.Drawing
$repoRoot = Split-Path $PSScriptRoot -Parent
$testOutput = Join-Path $repoRoot 'test-results'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$exe = Join-Path $repoRoot 'build\SteadyBrightness.exe'
$results = [Collections.Generic.List[string]]::new()
function Assert-True($condition, $label) {
 if (-not $condition) { throw "FAIL: $label" }
 $results.Add("PASS: $label")
 Write-Host "PASS: $label"
}
function Child-Controls([IntPtr]$window) {
 $script:controls = [Collections.Generic.List[object]]::new()
 $callback = [BrightnessTestNative+EnumProc]{ param($child,$unused)
  $text = [Text.StringBuilder]::new(512); $class = [Text.StringBuilder]::new(128)
  [void][BrightnessTestNative]::GetWindowText($child,$text,512)
  [void][BrightnessTestNative]::GetClassName($child,$class,128)
  $script:controls.Add([pscustomobject]@{Handle=$child;Class=$class.ToString();Text=$text.ToString();Id=[BrightnessTestNative]::GetDlgCtrlID($child);Visible=[BrightnessTestNative]::IsWindowVisible($child);Enabled=[BrightnessTestNative]::IsWindowEnabled($child)})
  return $true
 }
 [void][BrightnessTestNative]::EnumChildWindows($window,$callback,[IntPtr]::Zero)
 return $script:controls.ToArray()
}
function Launch-Test($argument) {
 $script:app = Start-Process -FilePath $exe -ArgumentList $argument -WindowStyle Hidden -PassThru
 $deadline = [DateTime]::UtcNow.AddSeconds(8)
 do {
  Start-Sleep -Milliseconds 100
  $script:hostWindow = [BrightnessTestNative]::FindOwnWindow($script:app.Id,'SteadyBrightness.TestHost.1')
  $script:flyout = [BrightnessTestNative]::FindOwnWindow($script:app.Id,'SteadyBrightness.Popup.1')
 } while (($script:hostWindow -eq [IntPtr]::Zero -or $script:flyout -eq [IntPtr]::Zero) -and [DateTime]::UtcNow -lt $deadline)
 Start-Sleep -Milliseconds 200
 Assert-True ($script:hostWindow -ne [IntPtr]::Zero) "host starts ($argument)"
 Assert-True ($script:flyout -ne [IntPtr]::Zero) "flyout starts ($argument)"
}
function Close-Test {
 if ($script:hostWindow -ne [IntPtr]::Zero) { [void][BrightnessTestNative]::PostMessage($script:hostWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
 if (-not $script:app.WaitForExit(3000)) { Stop-Process -Id $script:app.Id; throw 'Test process did not exit' }
 Start-Sleep -Milliseconds 100
}
if ($HelpersOnly) { return }
try {
 Launch-Test '--demo'
 $controls = Child-Controls $flyout
 Assert-True (@($controls | Where-Object { $_.Visible -and $_.Class -in @('msctls_trackbar32','SteadyBrightness.Slider.1') }).Count -eq 2) 'one independent slider per display'
 Assert-True (@($controls | Where-Object {$_.Text -eq '55%'}).Count -eq 1) 'first display initial value'
 Assert-True (@($controls | Where-Object {$_.Text -eq '40%'}).Count -eq 1) 'second display initial value'
 $slider = $controls | Where-Object Id -eq 100 | Select-Object -First 1
 [void][BrightnessTestNative]::SendMessage($slider.Handle,0x405,[IntPtr]1,[IntPtr]63)
 [void][BrightnessTestNative]::SendMessage($flyout,0x114,[IntPtr]8,$slider.Handle)
 Start-Sleep -Milliseconds 200
 $controls = Child-Controls $flyout
 Assert-True (@($controls | Where-Object {$_.Text -eq '63%'}).Count -eq 1) 'slider changes first display value'
 Assert-True (@($controls | Where-Object {$_.Text -eq '40%'}).Count -eq 1) 'changing first slider preserves second display'
 $rect = [BrightnessTestNative+Rect]::new(); [void][BrightnessTestNative]::GetWindowRect($flyout,[ref]$rect)
 $bitmap = [Drawing.Bitmap]::new(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top))
 $graphics = [Drawing.Graphics]::FromImage($bitmap); $dc = $graphics.GetHdc()
 [void][BrightnessTestNative]::PrintWindow($flyout,$dc,2); $graphics.ReleaseHdc($dc)
 $bitmap.Save((Join-Path $testOutput 'flyout-demo.png'),[Drawing.Imaging.ImageFormat]::Png)
 $graphics.Dispose(); $bitmap.Dispose()
 $before = [BrightnessTestNative]::GetGuiResources($app.Handle,0)
 1..100 | ForEach-Object {
  [void][BrightnessTestNative]::SendMessage($slider.Handle,0x405,[IntPtr]1,[IntPtr]($_%101))
  [void][BrightnessTestNative]::SendMessage($flyout,0x114,[IntPtr]5,$slider.Handle)
 }
 Start-Sleep -Milliseconds 400
 $after = [BrightnessTestNative]::GetGuiResources($app.Handle,0)
 Assert-True ($after -le $before+2) "no GDI growth after 100 slider events ($before -> $after)"
 [void][BrightnessTestNative]::PostMessage($flyout,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 Start-Sleep -Milliseconds 200
 Assert-True (-not [BrightnessTestNative]::IsWindowVisible($flyout)) 'close hides flyout instead of quitting tray'
 [void][BrightnessTestNative]::PostMessage($hostWindow,0x8002,[IntPtr]::Zero,[IntPtr]::Zero)
 Start-Sleep -Milliseconds 200
 Assert-True ([BrightnessTestNative]::IsWindowVisible($flyout)) 'second launch message reopens flyout'
 Close-Test

 Launch-Test '--test-crash'
 Start-Sleep -Milliseconds 700
 $controls = Child-Controls $flyout
 Assert-True (@($controls | Where-Object {$_.Text -like '*helper stopped*'}).Count -eq 1) 'worker crash produces recoverable status'
 Assert-True (-not $app.HasExited) 'worker crash does not crash tray'
 $res = [IntPtr]::Zero
 Assert-True ([BrightnessTestNative]::SendMessageTimeout($hostWindow,0,[IntPtr]::Zero,[IntPtr]::Zero,2,500,[ref]$res) -ne [IntPtr]::Zero) 'tray responds after worker crash'
 Close-Test

 Launch-Test '--test-timeout'
 $res = [IntPtr]::Zero
 Assert-True ([BrightnessTestNative]::SendMessageTimeout($hostWindow,0,[IntPtr]::Zero,[IntPtr]::Zero,2,500,[ref]$res) -ne [IntPtr]::Zero) 'tray responds during hung monitor command'
 Start-Sleep -Milliseconds 9500
 $controls = Child-Controls $flyout
 Assert-True (@($controls | Where-Object {$_.Text -like '*timed out*'}).Count -eq 1) 'hung command is bounded by timeout'
 Assert-True (-not $app.HasExited) 'hung helper leaves tray running'
 $children = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($app.Id)" | Where-Object Name -eq 'SteadyBrightness.exe'
 Assert-True (-not $children) 'timed-out helper is terminated'
 Close-Test

 $results | Set-Content -LiteralPath (Join-Path $testOutput 'integration-results.txt')
 $results
} finally {
 if ($script:app -and -not $script:app.HasExited) {
  [void][BrightnessTestNative]::PostMessage($script:hostWindow,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
  if (-not $script:app.WaitForExit(1500)) { Stop-Process -Id $script:app.Id }
 }
}
