@echo off
set "SHELLCHECK_PATH=%~dp0\tools\shellcheck\shellcheck.exe"

if not exist "%SHELLCHECK_PATH%" (
    echo ERROR: ShellCheck executable not found at %SHELLCHECK_PATH%
    echo Please run Utilities\install_shellcheck.bat to install ShellCheck.
    exit /b 1
)

"%SHELLCHECK_PATH%" %*
