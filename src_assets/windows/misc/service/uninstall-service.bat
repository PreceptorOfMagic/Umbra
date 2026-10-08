@echo off
setlocal enabledelayedexpansion
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"
set "SERVICE_BIN=%ROOT_DIR%\tools\sunshinesvc.exe"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0service-control.ps1" -Name ApolloService -Action Inspect -ExpectedPath "%SERVICE_BIN%"
if errorlevel 1 exit /b 1

set "SERVICE_CONFIG_DIR=%LOCALAPPDATA%\SudoMaker\Apollo"
set "SERVICE_CONFIG_FILE=%SERVICE_CONFIG_DIR%\service_start_type.txt"

rem Save the current service start type to a file if the service exists
sc qc ApolloService >nul 2>&1
if %ERRORLEVEL%==0 (
    if not exist "%SERVICE_CONFIG_DIR%\" mkdir "%SERVICE_CONFIG_DIR%\"

    rem Get the start type
    for /f "tokens=3" %%i in ('sc qc ApolloService ^| findstr /C:"START_TYPE"') do (
        set "CURRENT_START_TYPE=%%i"
    )

    rem Set the content to write
    if "!CURRENT_START_TYPE!"=="2" (
        sc qc ApolloService | findstr /C:"(DELAYED)" >nul
        if !ERRORLEVEL!==0 (
            set "CONTENT=2-delayed"
        ) else (
            set "CONTENT=2"
        )
    ) else if "!CURRENT_START_TYPE!" NEQ "" (
        set "CONTENT=!CURRENT_START_TYPE!"
    ) else (
        set "CONTENT=unknown"
    )

    rem Write content to file
    echo !CONTENT!> "%SERVICE_CONFIG_FILE%"
)

rem Stop only services whose executable belongs to this installation.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0service-control.ps1" -Name SunshineSvc -Action Remove -ExpectedPath "%SERVICE_BIN%"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0service-control.ps1" -Name ApolloService -Action Remove -ExpectedPath "%SERVICE_BIN%"
exit /b %ERRORLEVEL%
