@echo off
echo Building NovaHost in Release mode...
echo.

cd /d "%~dp0"
echo Current directory: %CD%
echo.

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"

rem Create the build directories if they don't exist
if not exist "Builds\VisualStudio2022\x64\Release" mkdir "Builds\VisualStudio2022\x64\Release"

echo Building NovaHost project...
%MSBUILD% NovaHost.sln /p:Configuration=Release /p:Platform=x64

if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code: %ERRORLEVEL%
    goto END
)

echo Build completed. Checking for the executable...
echo.

if exist "Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo Found executable at: Builds\VisualStudio2022\x64\Release\Nova Host.exe
) else (
    echo Executable not found at expected location.
    echo Searching for NovaHost executable...
    dir /s /b "NovaHost*.exe" 
    dir /s /b "*Nova*.exe"
)

:END
echo.
echo Process complete.
pause
