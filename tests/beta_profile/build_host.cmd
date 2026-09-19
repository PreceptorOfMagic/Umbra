@echo off
setlocal
set "PATH=%USERPROFILE%\msys64\ucrt64\bin;%PATH%"
cd /d "%~dp0..\..\build"
"%USERPROFILE%\msys64\ucrt64\bin\ninja.exe" -j2 sunshine
exit /b %errorlevel%
