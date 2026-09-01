@echo off
setlocal

set "COLLECTOR=%~dp0Collect-Apex4-Diagnostics.ps1"
if not exist "%COLLECTOR%" (
    echo Collector script not found:
    echo %COLLECTOR%
    pause
    exit /b 1
)

echo Connect and wake the Flydigi APEX 4 before continuing.
echo Branchez et allumez la Flydigi APEX 4 avant de continuer.
echo.

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%COLLECTOR%"
set "RESULT=%ERRORLEVEL%"

echo.
if not "%RESULT%"=="0" echo The collector failed with exit code %RESULT%.
pause
exit /b %RESULT%
