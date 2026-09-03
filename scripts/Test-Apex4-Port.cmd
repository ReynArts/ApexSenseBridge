@echo off
setlocal
title ApexSenseBridge - Test Apex 4
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Apex4-Port.ps1" -BridgeExecutable "%~dp0ApexSenseBridge.exe"
if errorlevel 1 (
  echo.
  echo Le script PowerShell a rencontre une erreur.
  pause
)
endlocal
