@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Drivers.ps1"
set "result=%ERRORLEVEL%"
echo.
if not "%result%"=="0" (
  echo Driver installation did not complete. See driver-install.log in this folder.
) else (
  echo Driver prerequisites are ready. Restart Windows before launching ApexSenseBridge.
)
pause
exit /b %result%
