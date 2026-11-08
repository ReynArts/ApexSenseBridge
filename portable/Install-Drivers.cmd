@echo off
setlocal
set "script=%~dp0Install-Drivers.ps1"
if not exist "%script%" goto :missing
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%script%"
set "result=%ERRORLEVEL%"
echo.
if not "%result%"=="0" goto :failed
echo Driver prerequisites are ready. Restart Windows before launching ApexSenseBridge.
goto :finish

:missing
set "result=2"
echo.
echo Install-Drivers.ps1 was not found next to this file:
echo   "%script%"
echo Extract the whole ApexSenseBridge-Portable.zip to a local drive, for example
echo C:\ApexSenseBridge-Portable, check that antivirus did not quarantine files,
echo then run this file again.
goto :finish

:failed
echo Driver installation did not complete. See driver-install.log in this folder.
echo Anything already installed is kept: run this file again to finish only what is missing.
echo If PowerShell said that the -File argument does not exist, copy the whole folder
echo to a local drive such as C:\ApexSenseBridge-Portable and run it from there.

:finish
pause
exit /b %result%
