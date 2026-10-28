param([switch]$OnlyInputHelpers)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-app.ps1') -HelpersOnly
Add-Type @'
using System;using System.Runtime.InteropServices;
public class SliderInputNative {
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct GuiInfo{public uint Size,Flags;public IntPtr Active,Focus,Capture,MenuOwner,MoveSize,Caret;public Rect CaretRect;}
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr to,int x,int y,int w,int h,IntPtr from,int sx,int sy,uint op);
 [DllImport("user32.dll")]public static extern bool RedrawWindow(IntPtr h,IntPtr rect,IntPtr region,uint flags);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetFocus(IntPtr h);
 [DllImport("user32.dll")]public static extern bool EnableWindow(IntPtr h,bool enabled);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll")]public static extern bool GetGUIThreadInfo(uint thread,ref GuiInfo info);
 [DllImport("kernel32.dll")]static extern IntPtr OpenProcess(uint flags,bool inherit,uint id);
 [DllImport("kernel32.dll")]static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr address,UIntPtr bytes,uint allocation,uint protect);
 [DllImport("kernel32.dll")]static extern bool ReadProcessMemory(IntPtr p,IntPtr address,byte[] data,UIntPtr bytes,out UIntPtr count);
 [DllImport("kernel32.dll")]static extern bool VirtualFreeEx(IntPtr p,IntPtr address,UIntPtr bytes,uint type);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr h);
 [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
 public static Rect ControlRect(IntPtr h,uint msg,uint expectedProcess){
  uint id;GetWindowThreadProcessId(h,out id);if(id!=expectedProcess)throw new Exception("Refusing to query another app");
  IntPtr p=OpenProcess(0x18,false,id);if(p==IntPtr.Zero)throw new Exception("Cannot open test process");
  IntPtr address=VirtualAllocEx(p,IntPtr.Zero,(UIntPtr)16,0x3000,4);
  try{
   if(address==IntPtr.Zero)throw new Exception("Cannot allocate test buffer");
   SendMessage(h,msg,IntPtr.Zero,address);byte[] bytes=new byte[16];UIntPtr count;
   if(!ReadProcessMemory(p,address,bytes,(UIntPtr)16,out count)||count.ToUInt64()!=16)throw new Exception("Cannot read control geometry");
   return new Rect{Left=BitConverter.ToInt32(bytes,0),Top=BitConverter.ToInt32(bytes,4),Right=BitConverter.ToInt32(bytes,8),Bottom=BitConverter.ToInt32(bytes,12)};
  }finally{if(address!=IntPtr.Zero)VirtualFreeEx(p,address,UIntPtr.Zero,0x8000);CloseHandle(p);}
 }
 public static IntPtr CaptureFor(IntPtr window){uint id;uint thread=GetWindowThreadProcessId(window,out id);var info=new GuiInfo();info.Size=(uint)Marshal.SizeOf<GuiInfo>();if(!GetGUIThreadInfo(thread,ref info))throw new Exception("Cannot read GUI state");return info.Capture;}
}
'@
$key='HKCU:\Software\SteadyBrightness'
$hadKey=Test-Path -LiteralPath $key
$saved=if($hadKey){Get-ItemProperty -LiteralPath $key -Name Theme -ErrorAction SilentlyContinue}else{$null}
$results=[Collections.Generic.List[string]]::new()
$travel=@{}
function Mouse($control,[uint32]$message,[int]$x,[int]$y=15,[int]$buttons=0){
 $position=($x -band 65535) -bor (($y -band 65535) -shl 16)
 [void][BrightnessTestNative]::SendMessage($control.Handle,$message,[IntPtr]$buttons,[IntPtr]$position)
}
function Value($control){return [int][BrightnessTestNative]::SendMessage($control.Handle,0x400,[IntPtr]::Zero,[IntPtr]::Zero)}
function Geometry($control,[uint32]$message){return [SliderInputNative]::ControlRect($control.Handle,$message,$app.Id)}
function X-For($control,[int]$percent){$r=$travel[$control.Id];return [int][Math]::Round($r[0]+($r[1]-$r[0])*$percent/100)}
function Cache-Travel($control){
 $value=Value $control
 [void][BrightnessTestNative]::SendMessage($control.Handle,0x405,[IntPtr]1,[IntPtr]0)
 $minimum=Geometry $control 0x419
 [void][BrightnessTestNative]::SendMessage($control.Handle,0x405,[IntPtr]1,[IntPtr]100)
 $maximum=Geometry $control 0x419
 [void][BrightnessTestNative]::SendMessage($control.Handle,0x405,[IntPtr]1,[IntPtr]$value)
 $travel[$control.Id]=@([Math]::Floor(($minimum.Left+$minimum.Right)/2),[Math]::Floor(($maximum.Left+$maximum.Right)/2))
}
function Capture-Client($control){
 $r=[SliderInputNative+Rect]::new();[void][SliderInputNative]::GetClientRect($control.Handle,[ref]$r)
 $bitmap=[Drawing.Bitmap]::new(($r.Right-$r.Left),($r.Bottom-$r.Top))
 $graphics=[Drawing.Graphics]::FromImage($bitmap);$target=$graphics.GetHdc();$source=[SliderInputNative]::GetDC($control.Handle)
 try{if(-not [SliderInputNative]::BitBlt($target,0,0,$bitmap.Width,$bitmap.Height,$source,0,0,0xcc0020)){throw 'Screen capture failed'}}
 finally{[void][SliderInputNative]::ReleaseDC($control.Handle,$source);$graphics.ReleaseHdc($target);$graphics.Dispose()}
 return $bitmap
}
function Difference($a,$b){
 $different=0
 for($y=0;$y -lt $a.Height;$y++){for($x=0;$x -lt $a.Width;$x++){if($a.GetPixel($x,$y).ToArgb() -ne $b.GetPixel($x,$y).ToArgb()){$different++}}}
 return $different
}
function Check-Paint($control,$name){
 Start-Sleep -Milliseconds 40
 $live=Capture-Client $control
 [void][SliderInputNative]::RedrawWindow($control.Handle,[IntPtr]::Zero,[IntPtr]::Zero,0x101)
 $fresh=Capture-Client $control
 try{
  $different=Difference $live $fresh
  $live.Save((Join-Path $testOutput ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
  if($different){$fresh.Save((Join-Path $testOutput ($name+'-fresh.png')),[Drawing.Imaging.ImageFormat]::Png)}

  Assert-True ($different -eq 0) "$name live painting matches full repaint ($different differences)"
 }finally{$live.Dispose();$fresh.Dispose()}
}
function Key($control,[int]$code){
 [void][BrightnessTestNative]::SendMessage($control.Handle,0x100,[IntPtr]$code,[IntPtr]::Zero)
 [void][BrightnessTestNative]::SendMessage($control.Handle,0x101,[IntPtr]$code,[IntPtr]::Zero)
}
if($OnlyInputHelpers){return}
try{
 Launch-Test '--demo'
 $sliders=@(Child-Controls $flyout|Where-Object { $_.Visible -and $_.Class -in @('msctls_trackbar32','SteadyBrightness.Slider.1') }|Sort-Object Id)
 foreach($control in $sliders){Cache-Travel $control}
 [uint32]$owner=0;[void][SliderInputNative]::GetWindowThreadProcessId($sliders[0].Handle,[ref]$owner)
 Assert-True ($owner -eq $app.Id) 'input tests target their own demo process'
 $labels=Child-Controls $flyout
 Assert-True (@($labels | Where-Object {$_.Class -eq 'Static' -and $_.Text -in @('Steady Brightness','Hardware brightness, per monitor.')}).Count -eq 0) 'heading and subtitle from the screenshot are removed'
 $firstRow=[BrightnessTestNative+Rect]::new();$panel=[BrightnessTestNative+Rect]::new()
 [void][BrightnessTestNative]::GetWindowRect(($labels|Where-Object {$_.Class -eq 'Static' -and $_.Text -like '1. XV271U*'}|Select-Object -First 1).Handle,[ref]$firstRow)
 [void][BrightnessTestNative]::GetWindowRect($flyout,[ref]$panel)
 Assert-True (($firstRow.Top-$panel.Top) -le 30) 'monitor controls move up into the removed header space'
 [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]12,[IntPtr]::Zero)
 [void][SliderInputNative]::SetForegroundWindow($flyout)
 Start-Sleep -Milliseconds 100
 $initial=Capture-Client $sliders[0]
 try{
  $thumb=Geometry $sliders[0] 0x419;$colors=[Collections.Generic.HashSet[int]]::new()
  for($y=$thumb.Top;$y -lt $thumb.Bottom;$y++){for($x=$thumb.Left;$x -lt $thumb.Right;$x++){[void]$colors.Add($initial.GetPixel($x,$y).ToArgb())}}
  Assert-True ($colors.Count -gt 10) 'resting thumb has blended antialiased edge colors'
  Assert-True (-not $colors.Contains([Drawing.Color]::Black.ToArgb())) 'resting thumb has no black native spot'
  $initial.Save((Join-Path $testOutput 'slider-resting-new.png'),[Drawing.Imaging.ImageFormat]::Png)
 }finally{$initial.Dispose()}
 $channel=Geometry $sliders[0] 0x41a
 Write-Host ('Native channel geometry: '+($channel|ConvertTo-Json -Compress))
 Mouse $sliders[0] 0x201 (X-For $sliders[0] 87) 15 1
 Assert-True ((Value $sliders[0]) -eq 87) 'track click jumps directly from 55% to 87% on mouse-down'
 Start-Sleep -Milliseconds 300
 Assert-True ((Value $sliders[0]) -eq 87) 'holding a track click does not page-repeat'
 Check-Paint $sliders[0] 'slider-dark-held'
 Mouse $sliders[0] 0x200 (X-For $sliders[0] 37) 15 1
 Assert-True ((Value $sliders[0]) -eq 37) 'click then drag follows pointer directly'
 Check-Paint $sliders[0] 'slider-dark-drag'
 Mouse $sliders[0] 0x200 -50 15 1
 Assert-True ((Value $sliders[0]) -eq 0) 'dragging beyond left end clamps at 0%'
 Mouse $sliders[0] 0x200 ($channel.Right+50) 15 1
 Assert-True ((Value $sliders[0]) -eq 100) 'dragging beyond right end clamps at 100%'
 Mouse $sliders[0] 0x202 ($channel.Right+50)
 Assert-True ([SliderInputNative]::CaptureFor($sliders[0].Handle) -eq [IntPtr]::Zero) 'mouse-up releases capture'
 $thumb=Geometry $sliders[0] 0x419;$center=[int][Math]::Floor(($thumb.Left+$thumb.Right)/2)
 Mouse $sliders[0] 0x201 ($center-3) 15 1
 Assert-True ((Value $sliders[0]) -eq 100) 'grabbing edge of thumb does not jump its value'
 Mouse $sliders[0] 0x200 ((X-For $sliders[0] 60)-3) 15 1
 Assert-True ((Value $sliders[0]) -eq 60) 'thumb dragging preserves grab offset'
 Mouse $sliders[0] 0x202 ((X-For $sliders[0] 60)-3)
 Key $sliders[0] 0x24;Assert-True ((Value $sliders[0]) -eq 0) 'Home goes to 0%'
 Key $sliders[0] 0x27;Assert-True ((Value $sliders[0]) -eq 1) 'Right arrow increments by 1%'
 Key $sliders[0] 0x22;Assert-True ((Value $sliders[0]) -eq 11) 'Page Down keeps the native 10% forward step'
 Key $sliders[0] 0x21;Assert-True ((Value $sliders[0]) -eq 1) 'Page Up keeps the native 10% backward step'
 Key $sliders[0] 0x23;Assert-True ((Value $sliders[0]) -eq 100) 'End goes to 100%'
 Key $sliders[0] 0x25;Assert-True ((Value $sliders[0]) -eq 99) 'Left arrow decrements by 1%'
 [void][BrightnessTestNative]::SendMessage($flyout,0x127,[IntPtr]0x10002,[IntPtr]::Zero)
 Check-Paint $sliders[0] 'slider-keyboard-focus'
 Mouse $sliders[0] 0x201 (X-For $sliders[0] 50) 15 1
 [void][BrightnessTestNative]::SendMessage($sliders[0].Handle,0x1f,[IntPtr]::Zero,[IntPtr]::Zero)
 Assert-True ([SliderInputNative]::CaptureFor($sliders[0].Handle) -eq [IntPtr]::Zero) 'cancelled drag releases capture cleanly'
 [void][SliderInputNative]::EnableWindow($sliders[0].Handle,$false)
 $before=Value $sliders[0];Mouse $sliders[0] 0x201 (X-For $sliders[0] 20) 15 1;Mouse $sliders[0] 0x202 (X-For $sliders[0] 20)
 Assert-True ((Value $sliders[0]) -eq $before) 'disabled slider ignores mouse clicks'
 Check-Paint $sliders[0] 'slider-disabled'
 [void][SliderInputNative]::EnableWindow($sliders[0].Handle,$true)
 $before=[BrightnessTestNative]::GetGuiResources($app.Handle,0)
 foreach($mode in @(11,12)){
  [void][BrightnessTestNative]::SendMessage($flyout,0x111,[IntPtr]$mode,[IntPtr]::Zero)
  foreach($control in $sliders){
   foreach($value in @(10,90,25,75,0,100,47,83)){
    Mouse $control 0x201 (X-For $control $value) 15 1
    Assert-True ([Math]::Abs((Value $control)-$value) -le 1) "theme $mode direct click reaches $value%"
    Mouse $control 0x202 (X-For $control $value)
   }
   Check-Paint $control ('slider-theme-'+$mode+'-monitor-'+$control.Id)
  }
 }
 $after=[BrightnessTestNative]::GetGuiResources($app.Handle,0)
 Assert-True ($after -le $before+1) "pointer and keyboard interactions do not leak GDI objects ($before -> $after)"
 $results|Set-Content -LiteralPath (Join-Path $testOutput 'slider-input-results.txt')
}finally{
 if($script:app -and -not $script:app.HasExited){Close-Test}
 if($saved -and $null -ne $saved.Theme){Set-ItemProperty -LiteralPath $key -Name Theme -Type DWord -Value $saved.Theme}
 elseif($hadKey){Remove-ItemProperty -LiteralPath $key -Name Theme -ErrorAction SilentlyContinue}
 elseif(Test-Path -LiteralPath $key){Remove-Item -LiteralPath $key}
}
