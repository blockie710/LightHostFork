@echo off
setlocal enabledelayedexpansion

echo Installing ShellCheck development tool...

:: Create tools directory if it doesn't exist
if not exist "%~dp0..\tools" mkdir "%~dp0..\tools"
if not exist "%~dp0..\tools\shellcheck" mkdir "%~dp0..\tools\shellcheck"

:: Set the ShellCheck version and URL
set "SHELLCHECK_VERSION=v0.9.0"
set "DOWNLOAD_URL=https://github.com/koalaman/shellcheck/releases/download/%SHELLCHECK_VERSION%/shellcheck-%SHELLCHECK_VERSION%.zip"
set "OUTPUT_FILE=%~dp0..\tools\shellcheck\shellcheck.zip"

echo Downloading ShellCheck %SHELLCHECK_VERSION%...
powershell -Command "(New-Object System.Net.WebClient).DownloadFile('%DOWNLOAD_URL%', '%OUTPUT_FILE%')"
if %ERRORLEVEL% neq 0 (
    echo Failed to download ShellCheck.
    exit /b 1
)

echo Extracting ShellCheck...
powershell -Command "Expand-Archive -Path '%OUTPUT_FILE%' -DestinationPath '%~dp0..\tools\shellcheck' -Force"
if %ERRORLEVEL% neq 0 (
    echo Failed to extract ShellCheck.
    exit /b 1
)

:: Clean up zip file
del "%OUTPUT_FILE%"

echo Creating convenience batch file...
(
    echo @echo off
    echo "%%~dp0\..\tools\shellcheck\shellcheck.exe" %%*
) > "%~dp0..\shellcheck.bat"

echo.
echo ShellCheck has been installed successfully!
echo You can now run shellcheck from the repository root:
echo   .\shellcheck.bat your-script.sh
echo.
echo Or add to PATH:
echo   set PATH=%%PATH%%;%~dp0..\tools\shellcheck
echo.

exit /b 0