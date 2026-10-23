@echo off
setlocal
cd /d "%~dp0"
if not defined VCToolsInstallDir (
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK.
    exit /b 1
  )
  for /f "usebackq tokens=*" %%v in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%v\VC\Auxiliary\Build\vcvars64.bat" >nul
)
if not defined VCToolsInstallDir (
  echo Could not find the MSVC x64 toolchain.
  exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Use an x64 Native Tools command prompt, or run this script outside a developer prompt.
  exit /b 1
)
if not exist build mkdir build
if errorlevel 1 exit /b 1
rc /nologo /fo build\app.res app.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O1 /Os /MT /W4 /EHsc /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_CRT_SECURE_NO_WARNINGS main.cpp monitor.cpp slider.cpp build\app.res /Fobuild\ /Febuild\SteadyBrightness.exe /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /DELAYLOAD:dxva2.dll /DELAYLOAD:UIAutomationCore.dll delayimp.lib /MANIFEST:EMBED /MANIFESTINPUT:app.manifest
exit /b %errorlevel%
