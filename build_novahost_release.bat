@echo off
echo Building NovaHost in Release mode for x64...
echo.

rem Initialize Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"

rem Change to project directory
cd /d "%~dp0"

rem Build NovaHost in Release mode for x64
msbuild NovaHost.sln /p:Configuration=Release /p:Platform=x64 /m /v:m

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed with error code: %ERRORLEVEL%
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
