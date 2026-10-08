@echo off

setlocal
pushd "%~dp0" || exit /b 1

nefconc.exe --remove-device-node --hardware-id root\sudomaker\sudovda --class-guid "4D36E968-E325-11CE-BFC1-08002BE10318"

set "UNINSTALL_RESULT=%ERRORLEVEL%"
popd
exit /b %UNINSTALL_RESULT%
