@echo off

setlocal

set "SCRIPT_DIR=%~dp0"
set "CLIENT_SCRIPT=%SCRIPT_DIR%AdminClient.ps1"

if not exist "%CLIENT_SCRIPT%" (
    echo [ERROR] AdminClient.ps1 script missing in directory!
    pause
    exit /b 1
)

REM bypass exec policy so powershell doesnt block script
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CLIENT_SCRIPT%" %*

set EXIT_CODE=%ERRORLEVEL%

if not "%*"=="" (
    exit /b %EXIT_CODE%
)

if not "%EXIT_CODE%"=="0" (
    echo.
    echo [ERROR] Powershell exited with error code: %EXIT_CODE%
    pause
)

endlocal