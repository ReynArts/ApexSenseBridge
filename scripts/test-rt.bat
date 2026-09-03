@echo off
setlocal
set EXE=%~dp0..\build-win\Release\ApexSenseBridge.exe
if not exist "%EXE%" (
  echo ApexSenseBridge.exe not found.
  echo Run scripts\build-windows.ps1 first.
  pause
  exit /b 1
)
"%EXE%" list
echo.
echo If exactly one APEX 4 or APEX 5 candidate is listed, press any key to run the gentle RT test.
pause >nul
"%EXE%" test-rt
echo.
pause
