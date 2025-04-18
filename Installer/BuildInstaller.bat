@echo off
echo Building Light Host Windows installer...

rem Check if Inno Setup is installed
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" (
    set ISCC="%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
) else if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" (
    set ISCC="%ProgramFiles%\Inno Setup 6\ISCC.exe"
) else (
    echo Error: Inno Setup 6 not found. Please install it from https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)

rem Check if VS2022 build exists
if not exist "..\Builds\VisualStudio2022\x64\Release\Light Host.exe" (
    echo Error: Light Host executable not found.
    echo Please build the Release configuration in Visual Studio 2022 first.
    pause
    exit /b 1
)

echo Building installer...
%ISCC% LightHostSetup.iss

if %ERRORLEVEL% NEQ 0 (
    echo Error: Installer build failed!
    pause
    exit /b 1
)

echo Installer successfully built!
echo You can find it in the Output folder.
pause