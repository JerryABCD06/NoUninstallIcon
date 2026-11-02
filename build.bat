@echo off
rem Build script for NoUninstallIcon (MSVC, no third-party dependencies).
rem Usage: build.bat [debug|release] [clean]
setlocal enabledelayedexpansion
pushd "%~dp0"

set "ROOT=%CD%"
set "BUILD=%ROOT%\build"
set "CONFIG=release"
if /i "%~1"=="debug" set "CONFIG=debug"

if /i "%~2"=="clean" (
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
)
if not exist "%BUILD%" mkdir "%BUILD%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" (
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%BUILD%\vsdir.txt" 2>nul
    set /p VSDIR=<"%BUILD%\vsdir.txt"
)
if not defined VSDIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VSDIR (
    echo [build] MSVC toolset not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build] vcvars64.bat failed.
    exit /b 1
)

set "CFLAGS=/nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I"%ROOT%\src""
set "LDFLAGS=/nologo /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:"%ROOT%\res\app.manifest""

if /i "%CONFIG%"=="debug" (
    set "CFLAGS=!CFLAGS! /Zi /Od /MDd /DDEBUG"
    set "LDFLAGS=!LDFLAGS! /DEBUG"
) else (
    set "CFLAGS=!CFLAGS! /O2 /MT /GL /DNDEBUG"
    set "LDFLAGS=!LDFLAGS! /LTCG /OPT:REF /OPT:ICF"
)

echo [build] resources
rc /nologo /I "%ROOT%\res" /fo "%BUILD%\app.res" "%ROOT%\res\app.rc"
if errorlevel 1 exit /b 1

echo [build] compile (%CONFIG%)
cl %CFLAGS% /Fo"%BUILD%\\" /Fd"%BUILD%\app.pdb" "%ROOT%\src\*.cpp" "%BUILD%\app.res" ^
  /link %LDFLAGS% /OUT:"%BUILD%\NoUninstallIcon.exe" ^
  d2d1.lib dwrite.lib d3d11.lib dxgi.lib dcomp.lib windowscodecs.lib ^
  shlwapi.lib propsys.lib taskschd.lib ole32.lib oleaut32.lib uuid.lib ^
  shell32.lib user32.lib gdi32.lib imm32.lib advapi32.lib dwmapi.lib uxtheme.lib dbghelp.lib uiautomationcore.lib
if errorlevel 1 (
    echo [build] FAILED
    popd
    exit /b 1
)

echo [build] ok: %BUILD%\NoUninstallIcon.exe
popd
endlocal

rem Language files must stay in sync with en.lang
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\check-lang.ps1"

rem Sign with the local certificate when one is installed
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\sign.ps1" -Path "%~dp0build\NoUninstallIcon.exe"
