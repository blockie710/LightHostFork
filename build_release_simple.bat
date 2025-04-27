@echo off
echo Building NovaHost in Release mode (simple approach)...

rem Find MSBuild.exe
set MSBUILD_PATH="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"

if not exist %MSBUILD_PATH% (
    echo ERROR: MSBuild not found at %MSBUILD_PATH%
    echo Searching for MSBuild...
    dir /s /b "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    pause
    exit /b 1
)

echo Using MSBuild from: %MSBUILD_PATH%
echo.

rem Build the solution
%MSBUILD_PATH% NovaHost.sln /p:Configuration=Release /p:Platform=x64

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed with error code %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully. 
echo Check for the executable at Builds\VisualStudio2022\x64\Release\Nova Host.exe
echo.

pause