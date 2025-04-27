@echo off
setlocal enabledelayedexpansion

:: NovaHost Unified Build Script
:: This script combines the best elements of all existing build scripts
:: with added flexibility and improved error handling

:: Parse command line arguments
set "CONFIG=Release"
set "PLATFORM=x64"
set "CLEAN=0"
set "VERBOSE=0"
set "BUILD_METHOD=msbuild"
set "PACKAGE=0"
set "TEST=0"
set "INSTALL=0"

:parse_args
if "%~1" == "" goto :end_parse_args
if /i "%~1" == "/?" goto :show_help
if /i "%~1" == "-?" goto :show_help
if /i "%~1" == "/h" goto :show_help
if /i "%~1" == "-h" goto :show_help
if /i "%~1" == "--help" goto :show_help
if /i "%~1" == "/c" set "CONFIG=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "-c" set "CONFIG=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "--config" set "CONFIG=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "/p" set "PLATFORM=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "-p" set "PLATFORM=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "--platform" set "PLATFORM=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "--clean" set "CLEAN=1" & shift & goto :parse_args
if /i "%~1" == "--verbose" set "VERBOSE=1" & shift & goto :parse_args
if /i "%~1" == "--method" set "BUILD_METHOD=%~2" & shift & shift & goto :parse_args
if /i "%~1" == "--package" set "PACKAGE=1" & shift & goto :parse_args
if /i "%~1" == "--test" set "TEST=1" & shift & goto :parse_args
if /i "%~1" == "--install" set "INSTALL=1" & shift & goto :parse_args
shift
goto :parse_args

:show_help
echo.
echo NovaHost Unified Build Script
echo Usage: build_nova_improved.bat [options]
echo.
echo Options:
echo   --config ^<config^>    Build configuration (Debug or Release, default: Release)
echo   --platform ^<platform^> Build platform (Win32 or x64, default: x64)
echo   --clean              Clean before building
echo   --verbose            Show detailed build output
echo   --method ^<method^>    Build method (msbuild, devenv, or simple, default: msbuild)
echo   --package            Create a distributable package after successful build
echo   --test               Run automated tests after successful build
echo   --install            Run installer build after successful build
echo.
echo Examples:
echo   build_nova_improved.bat --config Debug
echo   build_nova_improved.bat --platform Win32 --verbose
echo   build_nova_improved.bat --clean --method devenv
echo.
exit /b 0

:end_parse_args

:: Set title with build configuration
title NovaHost Build - %CONFIG% (%PLATFORM%)

:: Setup colors for better readability
set "GREEN=[92m"
set "YELLOW=[93m"
set "RED=[91m"
set "BLUE=[94m"
set "RESET=[0m"

:: Create log file with timestamp
set "TIMESTAMP=%date:~-4,4%%date:~-7,2%%date:~-10,2%%time:~0,2%%time:~3,2%%time:~6,2%"
set "TIMESTAMP=%TIMESTAMP: =0%"
set "LOG_FILE=build_log_%TIMESTAMP%.txt"

:: Change to script directory
cd /d "%~dp0"

echo %GREEN%=========================================%RESET%
echo %GREEN%       NovaHost Build System             %RESET%
echo %GREEN%=========================================%RESET%
echo.
echo %BLUE%Current directory: %CD%%RESET%
echo %BLUE%Build Configuration: %CONFIG%%RESET%
echo %BLUE%Build Platform: %PLATFORM%%RESET%
echo %BLUE%Build Method: %BUILD_METHOD%%RESET%
echo %BLUE%Log File: %LOG_FILE%%RESET%
echo.

:: Create the build directories if they don't exist
if not exist "Builds\VisualStudio2022\%PLATFORM%\%CONFIG%" (
    echo Creating build directory...
    mkdir "Builds\VisualStudio2022\%PLATFORM%\%CONFIG%"
)

:: Backup the project file if needed
if not exist "NovaHost.vcxproj.backup" (
    echo Backing up project file...
    copy "NovaHost.vcxproj" "NovaHost.vcxproj.backup" > nul
)

:: Detect Visual Studio installation
echo %YELLOW%Detecting Visual Studio installation...%RESET%
set "VS_PATH="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    set "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
    set "DEVENV=%VS_PATH%\Common7\IDE\devenv.com"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    set "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
    set "DEVENV=%VS_PATH%\Common7\IDE\devenv.com"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    set "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
    set "DEVENV=%VS_PATH%\Common7\IDE\devenv.com"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
    set "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
    set "DEVENV=%VS_PATH%\Common7\IDE\devenv.com"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise"
    set "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
    set "DEVENV=%VS_PATH%\Common7\IDE\devenv.com"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" (
    set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    set "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
    set "DEVENV=%VS_PATH%\Common7\IDE\devenv.com"
)

if "%VS_PATH%" == "" (
    echo %RED%Error: Visual Studio installation not found.%RESET%
    echo Please install Visual Studio 2019 or 2022 with C++ development tools.
    goto :ERROR
)

echo %GREEN%Found Visual Studio at: %VS_PATH%%RESET%

:: Clean if requested
if "%CLEAN%" == "1" (
    echo %YELLOW%Cleaning previous build...%RESET%
    if "%BUILD_METHOD%" == "msbuild" (
        %MSBUILD% NovaHost.sln /t:Clean /p:Configuration=%CONFIG% /p:Platform=%PLATFORM%
    ) else if "%BUILD_METHOD%" == "devenv" (
        %DEVENV% NovaHost.sln /Clean "%CONFIG%|%PLATFORM%"
    ) else (
        if exist "Builds\VisualStudio2022\%PLATFORM%\%CONFIG%" rmdir /s /q "Builds\VisualStudio2022\%PLATFORM%\%CONFIG%"
        mkdir "Builds\VisualStudio2022\%PLATFORM%\%CONFIG%"
    )
)

:: Build using selected method
echo %YELLOW%Building NovaHost in %CONFIG% mode for %PLATFORM%...%RESET%

set "START_TIME=%time%"

if "%BUILD_METHOD%" == "msbuild" (
    if "%VERBOSE%" == "1" (
        %MSBUILD% NovaHost.sln /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /v:detailed /fl /flp:logfile=%LOG_FILE%;verbosity=detailed
    ) else (
        %MSBUILD% NovaHost.sln /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /v:minimal /fl /flp:logfile=%LOG_FILE%;verbosity=minimal
    )
) else if "%BUILD_METHOD%" == "devenv" (
    if "%VERBOSE%" == "1" (
        %DEVENV% NovaHost.sln /Build "%CONFIG%|%PLATFORM%" /Out %LOG_FILE%
    ) else (
        %DEVENV% NovaHost.sln /Build "%CONFIG%|%PLATFORM%" /Out %LOG_FILE% /QuietBuild
    )
) else if "%BUILD_METHOD%" == "simple" (
    %MSBUILD% NovaHost.sln /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:OutDir="Builds\VisualStudio2022\%PLATFORM%\%CONFIG%\\" /p:TargetName="Nova Host" /fl /flp:logfile=%LOG_FILE%
) else (
    echo %RED%Unknown build method: %BUILD_METHOD%%RESET%
    goto :ERROR
)

if %ERRORLEVEL% NEQ 0 (
    echo %RED%Build failed with error code: %ERRORLEVEL%%RESET%
    goto :ERROR
)

set "END_TIME=%time%"

:: Calculate build duration
call :calculate_duration "%START_TIME%" "%END_TIME%"

echo %GREEN%Build completed successfully in %DURATION%!%RESET%

:: Check if executable exists
set "EXE_PATH=Builds\VisualStudio2022\%PLATFORM%\%CONFIG%\Nova Host.exe"

if exist "%EXE_PATH%" (
    echo %GREEN%Found executable at: %EXE_PATH%%RESET%
    
    :: Get file size and timestamp
    for %%A in ("%EXE_PATH%") do (
        set "EXE_SIZE=%%~zA"
        set "EXE_DATE=%%~tA"
    )
    
    echo %BLUE%File size: !EXE_SIZE! bytes%RESET%
    echo %BLUE%Last modified: !EXE_DATE!%RESET%
) else (
    echo %YELLOW%Executable not found at expected location.%RESET%
    echo %YELLOW%Searching for NovaHost executable...%RESET%
    dir /s /b "NovaHost*.exe" 2>nul
    dir /s /b "*Nova*.exe" 2>nul
)

:: Run tests if requested
if "%TEST%" == "1" (
    echo.
    echo %YELLOW%Running automated tests...%RESET%
    if exist "RunPerformanceTests.bat" (
        call RunPerformanceTests.bat
    ) else if exist "Utilities\RunPerformanceTests.bat" (
        call Utilities\RunPerformanceTests.bat
    ) else (
        echo %RED%Test script not found.%RESET%
    )
)

:: Package if requested
if "%PACKAGE%" == "1" (
    echo.
    echo %YELLOW%Creating distribution package...%RESET%
    if exist "Utilities\PackageForTesters.bat" (
        call Utilities\PackageForTesters.bat
    ) else if exist "PackageForTesters.bat" (
        call PackageForTesters.bat
    ) else (
        echo %RED%Packaging script not found.%RESET%
    )
)

:: Build installer if requested
if "%INSTALL%" == "1" (
    echo.
    echo %YELLOW%Building installer package...%RESET%
    if exist "Installer\BuildNovaHostInstaller.bat" (
        call Installer\BuildNovaHostInstaller.bat
    ) else (
        echo %RED%Installer build script not found.%RESET%
    )
)

echo.
echo %GREEN%Process complete.%RESET%
goto :END

:ERROR
echo.
echo %RED%Build process failed.%RESET%
echo See %LOG_FILE% for details.

:END
echo.
pause
exit /b %ERRORLEVEL%

:: Calculate duration between two times
:calculate_duration
setlocal
set "start=%~1"
set "end=%~2"

:: Convert start time to seconds
set /a "start_h=1%start:~0,2%-100"
set /a "start_m=1%start:~3,2%-100"
set /a "start_s=1%start:~6,2%-100"
set /a "start_total=start_h*3600 + start_m*60 + start_s"

:: Convert end time to seconds
set /a "end_h=1%end:~0,2%-100"
set /a "end_m=1%end:~3,2%-100"
set /a "end_s=1%end:~6,2%-100"
set /a "end_total=end_h*3600 + end_m*60 + end_s"

:: Handle day boundary
if %end_total% LSS %start_total% set /a "end_total+=86400"

:: Calculate difference
set /a "diff_total=end_total-start_total"
set /a "diff_h=diff_total/3600"
set /a "diff_m=(diff_total%%3600)/60"
set /a "diff_s=diff_total%%60"

:: Format the output
if %diff_h% LSS 10 set "diff_h=0%diff_h%"
if %diff_m% LSS 10 set "diff_m=0%diff_m%"
if %diff_s% LSS 10 set "diff_s=0%diff_s%"

endlocal & set "DURATION=%diff_h%:%diff_m%:%diff_s%"
exit /b 0