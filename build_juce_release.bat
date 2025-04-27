@echo off
echo JUCE NovaHost Release Build Script
echo ================================
echo.

rem Define paths
set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional
set VS_DEVENV="%VS_PATH%\Common7\IDE\devenv.com"

rem Check for JUCE project file
if not exist "NovaHost.jucer" (
    echo ERROR: NovaHost.jucer not found in current directory
    goto ERROR
)

echo Step 1: Ensuring Builds directory structure exists...
if not exist "Builds" mkdir Builds
if not exist "Builds\VisualStudio2022" mkdir Builds\VisualStudio2022
if not exist "Builds\VisualStudio2022\x64" mkdir Builds\VisualStudio2022\x64
if not exist "Builds\VisualStudio2022\x64\Release" mkdir Builds\VisualStudio2022\x64\Release

echo.
echo Step 2: Building NovaHost (Release, x64)...
echo.

rem Try with devenv.com
echo Building with Visual Studio devenv.com...
%VS_DEVENV% NovaHost.sln /rebuild "Release|x64"

echo.
echo Step 3: Checking for built executable...
echo.

rem Define possible output locations
set POSSIBLE_LOCATIONS=^
Builds\VisualStudio2022\x64\Release\NovaHost.exe^
Builds\VisualStudio2022\x64\Release\Nova Host.exe^
x64\Release\NovaHost.exe^
x64\Release\Nova Host.exe^
NovaHost\x64\Release\NovaHost.exe^
NovaHost\x64\Release\Nova Host.exe^
Release\NovaHost.exe^
Release\Nova Host.exe

rem Look for executable in possible locations
set FOUND_EXE=0
for %%i in (%POSSIBLE_LOCATIONS%) do (
    if exist "%%i" (
        echo Found executable at: %%i
        echo Copying to expected location for PackageForTesters.bat...
        
        rem Create target directory if it doesn't exist
        if not exist "Builds\VisualStudio2022\x64\Release" mkdir Builds\VisualStudio2022\x64\Release
        
        rem Copy executable to expected location
        copy "%%i" "Builds\VisualStudio2022\x64\Release\Nova Host.exe"
        echo Copied executable to Builds\VisualStudio2022\x64\Release\Nova Host.exe
        set FOUND_EXE=1
    )
)

if %FOUND_EXE%==0 (
    echo ERROR: NovaHost executable not found after build.
    echo Please verify that Visual Studio is properly installed and can build the project.
    goto ERROR
)

echo.
echo Build completed successfully. The NovaHost executable is now available at:
echo Builds\VisualStudio2022\x64\Release\Nova Host.exe
echo.
echo You can now run the PackageForTesters.bat script in the Utilities folder
echo to create a distribution package for testing.
goto END

:ERROR
echo.
echo Build process failed.
exit /b 1

:END
pause