@echo off
echo Building NovaHost in Release mode using Visual Studio's devenv...
echo.

rem Find Visual Studio devenv.com
set DEVENV="C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.com"

if not exist %DEVENV% (
    echo ERROR: Visual Studio devenv.com not found at expected location.
    echo Trying to find it elsewhere...
    dir /s /b "C:\Program Files\Microsoft Visual Studio\2022\*\Common7\IDE\devenv.com"
    echo.
    echo Please update this script with the correct path to devenv.com
    pause
    exit /b 1
)

echo Using Visual Studio at: %DEVENV%
echo.
echo Building NovaHost in Release mode for x64 platform...
%DEVENV% NovaHost.sln /Build "Release|x64" /Out build_log.txt

if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code %ERRORLEVEL%
    echo Check build_log.txt for details.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed. Checking for executable...
echo.

if exist "Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo Build successful! Executable found at:
    echo Builds\VisualStudio2022\x64\Release\Nova Host.exe
) else (
    echo Build may have succeeded but executable not found at the expected location.
    echo Searching for Nova Host.exe in the workspace...
    dir /s /b "Nova Host.exe"
)

echo.
echo You can now run the PackageForTesters.bat script in the Utilities folder
echo to create a distribution package for testing.
echo.

pause