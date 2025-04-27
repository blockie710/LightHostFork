@echo off
echo NovaHost Release Build Script
echo ============================
echo.

rem Set up Visual Studio environment
echo Setting up Visual Studio environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to set up Visual Studio environment
    goto ERROR
)

rem Navigate to project directory
cd /d "%~dp0"
echo Current directory: %CD%

rem Check for JUCE project file
if not exist "NovaHost.jucer" (
    echo Error: JUCE project file (NovaHost.jucer) not found
    goto ERROR
)

echo.
echo Checking if Builds directory exists...
if not exist "Builds" mkdir Builds

echo.
echo Starting build process for NovaHost Release (x64)...

rem Create build directory if it doesn't exist
if not exist "Builds\VisualStudio2022\x64\Release" (
    mkdir "Builds\VisualStudio2022\x64\Release"
)

echo.
echo Building NovaHost using devenv...
"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.com" NovaHost.sln /rebuild "Release|x64" /out build_log.txt
if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code: %ERRORLEVEL%
    echo See build_log.txt for details
    goto ERROR
)

echo.
echo Checking for built executable...
if exist "Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo Build successful! NovaHost executable created.
    echo Location: %CD%\Builds\VisualStudio2022\x64\Release\Nova Host.exe
) else (
    echo Error: Build completed but executable not found in expected location.
    echo Expected: %CD%\Builds\VisualStudio2022\x64\Release\Nova Host.exe
    goto ERROR
)

echo.
echo Build process completed successfully!
echo.
echo You can now run PackageForTesters.bat if you want to create a distribution package.
goto END

:ERROR
echo.
echo Build process failed. Please check the errors above.
exit /b 1

:END
pause