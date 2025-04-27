@echo off
echo Building NovaHost in Release mode for x64...
echo.

rem Initialize Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"

rem Change to project directory
cd /d "%~dp0"

echo.
echo Environment initialized. Building NovaHost...
echo.

rem Build NovaHost in Release mode for x64 with detailed output
echo Running: msbuild NovaHost.sln /p:Configuration=Release /p:Platform=x64 /v:detailed
msbuild NovaHost.sln /p:Configuration=Release /p:Platform=x64 /v:detailed

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed with error code: %ERRORLEVEL%
    echo.
    echo Checking for common issues...
    
    if not exist "NovaHost.sln" (
        echo ERROR: NovaHost.sln not found in current directory.
        echo Current directory: %CD%
        echo Files in current directory:
        dir
    )
    
    echo.
    echo Please check the output above for specific error messages.
    echo.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully!
echo.
echo The release build of NovaHost should now be available at Builds\VisualStudio2022\x64\Release\Nova Host.exe
echo or in a similar location depending on your project configuration.
echo.
pause
