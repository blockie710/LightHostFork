@echo off
setlocal enabledelayedexpansion

:: NovaHost CMake Build Script
:: This script builds NovaHost using CMake for improved cross-platform compatibility

:: Parse command line arguments
set "CONFIG=Release"
set "CLEAN=0"
set "RUN_TESTS=0"
set "CMAKE_GEN="

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
if /i "%~1" == "--clean" set "CLEAN=1" & shift & goto :parse_args
if /i "%~1" == "--tests" set "RUN_TESTS=1" & shift & goto :parse_args
if /i "%~1" == "--generator" set "CMAKE_GEN=%~2" & shift & shift & goto :parse_args
shift
goto :parse_args

:show_help
echo.
echo NovaHost CMake Build Script
echo Usage: build_with_cmake.bat [options]
echo.
echo Options:
echo   --config ^<config^>      Build configuration (Debug or Release, default: Release)
echo   --clean                Clean before building
echo   --tests                Build and run tests
echo   --generator ^<genname^>  Specify CMake generator
echo.
echo Examples:
echo   build_with_cmake.bat --config Debug
echo   build_with_cmake.bat --clean --tests
echo   build_with_cmake.bat --generator "Visual Studio 17 2022"
echo.
exit /b 0

:end_parse_args

:: Set title with build configuration
title NovaHost CMake Build - %CONFIG%

echo =========================================
echo       NovaHost CMake Build System
echo =========================================
echo.

:: Change to script directory
cd /d "%~dp0"
echo Current directory: %CD%
echo.

:: Create CMakeBuild directory if it doesn't exist
if not exist "CMakeBuild" mkdir CMakeBuild
cd CMakeBuild

:: Clean if requested
if "%CLEAN%" == "1" (
    echo Cleaning previous build...
    if exist "build" rmdir /s /q build
    if exist "install" rmdir /s /q install
)

:: Create the build directory if it doesn't exist
if not exist "build" mkdir build
cd build

echo Configuring CMake...

:: Set generator command line if specified
set "GEN_CMD="
if not "%CMAKE_GEN%" == "" (
    set "GEN_CMD=-G "%CMAKE_GEN%""
)

:: Configure with CMake
if "%RUN_TESTS%" == "1" (
    cmake %GEN_CMD% -DCMAKE_BUILD_TYPE=%CONFIG% -DNOVAHOST_BUILD_TESTS=ON ../..
) else (
    cmake %GEN_CMD% -DCMAKE_BUILD_TYPE=%CONFIG% -DNOVAHOST_BUILD_TESTS=OFF ../..
)

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed with error code: %ERRORLEVEL%
    goto END
)

echo.
echo Building NovaHost with CMake in %CONFIG% mode...
echo.

:: Build using CMake
cmake --build . --config %CONFIG%

if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code: %ERRORLEVEL%
    goto END
)

echo.
echo Build completed successfully!

:: Execute tests if requested
if "%RUN_TESTS%" == "1" (
    echo.
    echo Running tests...
    ctest -C %CONFIG% --output-on-failure
)

:: Report build status
echo.
if "%CONFIG%" == "Debug" (
    if exist "bin\Debug\Nova Host.exe" (
        echo Found executable at: bin\Debug\Nova Host.exe
    ) else if exist "bin\Nova Host.exe" (
        echo Found executable at: bin\Nova Host.exe
    ) else (
        echo Searching for NovaHost executable...
        dir /s /b "Nova*.exe" 2>nul
    )
) else (
    if exist "bin\Release\Nova Host.exe" (
        echo Found executable at: bin\Release\Nova Host.exe
    ) else if exist "bin\Nova Host.exe" (
        echo Found executable at: bin\Nova Host.exe
    ) else (
        echo Searching for NovaHost executable...
        dir /s /b "Nova*.exe" 2>nul
    )
)

:END
echo.
echo Process complete.
cd /d "%~dp0"
pause
exit /b %ERRORLEVEL%