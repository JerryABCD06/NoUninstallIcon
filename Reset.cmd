@echo off
rem ---------------------------------------------------------------------------
rem  Reset - undo everything this program did
rem
rem  Restores every shortcut the program hid, putting back the attributes it
rem  found, removes the sign-in task, and stops the background watcher. The
rem  program and its data stay where they are; this is the portable build's
rem  equivalent of uninstalling.
rem
rem  Elevation is required: the shortcuts being restored can live under
rem  %ProgramData%, and the sign-in task is machine-wide. The script asks for it
rem  once, then reports what the program returned.
rem ---------------------------------------------------------------------------
setlocal
set "EXE=%~dp0NoUninstallIcon.exe"

if not exist "%EXE%" (
    echo.
    echo   NoUninstallIcon.exe was not found next to this script.
    echo   Keep Reset.cmd in the same folder as the program and run it again.
    echo.
    pause
    exit /b 1
)

echo.
echo   Restoring every hidden shortcut and removing the sign-in task...
echo   A permission prompt may appear.
echo.

set "NOUI_RESET_EXE=%EXE%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { $p = Start-Process -FilePath $env:NOUI_RESET_EXE -ArgumentList '--cleanup' -Verb RunAs -Wait -PassThru } catch { exit 2 }; exit $p.ExitCode"
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo   Done. Every shortcut is back to the attributes it had before this
    echo   program touched it, and no sign-in task remains.
    echo.
    echo   The program and its data are still in this folder. Delete the folder
    echo   to remove them.
) else if "%RESULT%"=="2" (
    echo   Cancelled, or the permission prompt could not be shown. Nothing was
    echo   changed.
) else (
    echo   The program reported a problem ^(exit code %RESULT%^). Nothing was
    echo   changed that could not be changed back, but some shortcuts may still
    echo   be hidden.
    echo.
    echo   To see why, run this from an elevated Command Prompt:
    echo.
    echo       NoUninstallIcon.exe --cleanup --log
    echo.
    echo   and then read log.txt in the data folder next to the program.
)
echo.
pause
exit /b %RESULT%