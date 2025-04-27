@echo off
setlocal

set "SHELLCHECK_PATH=%~dp0..\tools\shellcheck"

REM Check if ShellCheck exists
if not exist "%SHELLCHECK_PATH%\shellcheck.exe" (
    echo ERROR: ShellCheck not found at %SHELLCHECK_PATH%\shellcheck.exe
    echo Please run install_shellcheck.bat first.
    exit /b 1
)

REM Add ShellCheck to the PATH for this session
set "PATH=%PATH%;%SHELLCHECK_PATH%"

echo ShellCheck has been added to your PATH for this session.
echo You can now run 'shellcheck' directly from the command line.
echo.
echo To verify, run: where shellcheck
echo.
echo To open a command prompt with ShellCheck in the PATH, run this script.

REM Launch a new command prompt with the updated PATH
cmd /k echo ShellCheck is now available in this command prompt.