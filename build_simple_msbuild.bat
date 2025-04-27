@echo off
cd /d "%~dp0"
echo Current directory: %CD%
echo.
echo Building NovaHost in Release mode (x64)...
echo.

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"

rem Ensure the expected build output directory exists
if not exist "Builds\VisualStudio2022\x64\Release\" mkdir "Builds\VisualStudio2022\x64\Release\"

echo Building with MSBuild...
%MSBUILD% NovaHost.sln /p:Configuration=Release /p:Platform=x64 /p:OutDir="Builds\VisualStudio2022\x64\Release\" /p:TargetName="Nova Host"

echo.
echo Checking for the executable...
if exist "Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo SUCCESS! Executable built at:
    echo Builds\VisualStudio2022\x64\Release\Nova Host.exe
) else (
    echo Executable not found at expected location.
    echo Looking for it elsewhere...
    dir /s /b "*.exe" | findstr "Nova"
)

echo.
echo Process completed.
pause