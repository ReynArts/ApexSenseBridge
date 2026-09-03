@echo off
setlocal
title ApexSenseBridge - Session complete Apex 4
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Apex4-Full-Session.ps1" -BridgeExecutable "%~dp0ApexSenseBridge.exe" -ViiperLibrary "%~dp0libVIIPER.dll"
set "result=%ERRORLEVEL%"
echo.
if not "%result%"=="0" echo Le test de session s'est termine avec le code %result%.
pause
exit /b %result%
