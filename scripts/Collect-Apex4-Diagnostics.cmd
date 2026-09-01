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
echo Use its USB cable or its own 2.4 GHz receiver, not Bluetooth.
echo Utilisez son cable USB ou son recepteur 2,4 GHz, pas le Bluetooth.
echo Switch the APEX 4 to DInput before continuing: hold FN + A for about 3 seconds,
echo or select DInput in the controller LCD connection menu. The screen should show D.
echo Passez l'APEX 4 en DInput avant de continuer : maintenez FN + A environ 3 secondes,
echo ou choisissez DInput dans le menu de connexion de l'ecran. L'ecran doit afficher D.
echo Disconnect every other Flydigi controller or receiver.
echo Debranchez toute autre manette ou tout autre recepteur Flydigi.
echo.

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%COLLECTOR%"
set "RESULT=%ERRORLEVEL%"

echo.
if not "%RESULT%"=="0" echo The collector failed with exit code %RESULT%.
pause
exit /b %RESULT%
