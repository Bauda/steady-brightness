$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test-slider-input.ps1') -OnlyInputHelpers
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName UIAutomationClient
$results=[Collections.Generic.List[string]]::new()
try{
 Launch-Test '--demo'
 $controls=@(Child-Controls $flyout|Where-Object Class -eq 'SteadyBrightness.Slider.1'|Sort-Object Id)
 $element=[System.Windows.Automation.AutomationElement]::FromHandle($controls[0].Handle)
 Assert-True ($element.Current.ControlType -eq [System.Windows.Automation.ControlType]::Slider) 'custom control exposes the Slider accessibility role'
 Assert-True ($element.Current.Name -like '*XV271U M3*') 'slider exposes its monitor name'
 $range=[System.Windows.Automation.RangeValuePattern]$element.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern)
 Assert-True ($range.Current.Minimum -eq 0 -and $range.Current.Maximum -eq 100) 'accessible brightness range is 0 to 100'
 Assert-True ($range.Current.Value -eq 55) 'accessible value matches the initial position'
 $range.SetValue(63)
 Start-Sleep -Milliseconds 150
 Assert-True ((Value $controls[0]) -eq 63) 'accessible SetValue changes brightness'
 Assert-True ((Value $controls[1]) -eq 40) 'accessible value change preserves the other monitor'
 $rejected=$false;try{$range.SetValue(101)}catch{$rejected=$true}
 Assert-True $rejected 'accessible SetValue rejects out-of-range brightness'
 [void][SliderInputNative]::EnableWindow($controls[0].Handle,$false)
 Assert-True (-not $element.Current.IsEnabled -and $range.Current.IsReadOnly) 'disabled slider reports disabled and read-only'
 $rejected=$false;try{$range.SetValue(70)}catch{$rejected=$true}
 Assert-True $rejected 'disabled accessible slider rejects changes'
 [void][SliderInputNative]::EnableWindow($controls[0].Handle,$true)
 $range.SetValue(68)
 Assert-True ((Value $controls[0]) -eq 68) 'accessibility control recovers after enabling'
 Close-Test
 $results|Set-Content -LiteralPath (Join-Path $testOutput 'slider-accessibility-results.txt')
}finally{
 if($script:app -and -not $script:app.HasExited){Close-Test}
}
