@echo off
echo NovaHost Tester Distribution Package Creator
echo ==========================================
echo.

rem Set version from parameter or use default
set VERSION=%1
if "%VERSION%"=="" set VERSION=1.0.0-beta

rem Set date in format YYYYMMDD
set NOW=%date:~-4,4%%date:~-7,2%%date:~-10,2%

rem Create package directory structure
set PACKAGE_DIR=..\build\NovaHost-Tester-Package-%VERSION%-%NOW%
echo Creating package directory: %PACKAGE_DIR%
if exist %PACKAGE_DIR% rmdir /s /q %PACKAGE_DIR%
mkdir %PACKAGE_DIR%
mkdir %PACKAGE_DIR%\bin
mkdir %PACKAGE_DIR%\logs
mkdir %PACKAGE_DIR%\docs
mkdir %PACKAGE_DIR%\tools

rem Check if NovaHost executable exists
if not exist "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo ERROR: NovaHost executable not found.
    echo Please build NovaHost in Release mode first.
    goto END
)

echo.
echo Building installer for distribution...
cd ..\Installer
call BuildNovaHostInstaller.bat > nul
cd ..\Utilities

echo.
echo Copying files to package directory...
rem Copy main executable and DLLs
copy "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" "%PACKAGE_DIR%\bin\" > nul
copy "..\Builds\VisualStudio2022\x64\Release\*.dll" "%PACKAGE_DIR%\bin\" > nul

rem Copy debug config and utilities
copy "NovaHost_Debug.config" "%PACKAGE_DIR%\bin\" > nul
copy "Reset Settings.command" "%PACKAGE_DIR%\tools\" > nul
copy "RunPerformanceTests.bat" "%PACKAGE_DIR%\tools\" > nul

rem Copy license and documentation
copy "..\license" "%PACKAGE_DIR%\docs\license.txt" > nul
copy "..\gpl.txt" "%PACKAGE_DIR%\docs\" > nul
copy "..\readme.md" "%PACKAGE_DIR%\docs\readme.txt" > nul
copy "NovaHost_TestingFeedback.md" "%PACKAGE_DIR%\docs\" > nul

rem Copy installer
copy "..\Installer\Output\NovaHostSetup*.exe" "%PACKAGE_DIR%\" > nul

echo.
echo Creating README for testers...
echo # NovaHost Test Build %VERSION% > "%PACKAGE_DIR%\README.txt"
echo Built on: %date% %time% >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo Thank you for helping test NovaHost! >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo ## Contents of this package: >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo - bin/ - Contains the executable and required DLLs >> "%PACKAGE_DIR%\README.txt"
echo - docs/ - Documentation and the testing feedback form >> "%PACKAGE_DIR%\README.txt"
echo - tools/ - Utilities for testing and troubleshooting >> "%PACKAGE_DIR%\README.txt"
echo - NovaHostSetup-*.exe - Installer for standard installation >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo ## Installation Options: >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo 1. Standard Installation (Recommended): >> "%PACKAGE_DIR%\README.txt"
echo    Run the NovaHostSetup-*.exe file and follow the prompts. >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo 2. Portable Mode: >> "%PACKAGE_DIR%\README.txt"
echo    You can run directly from the bin folder without installation. >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo ## Reporting Issues: >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo Please use the testing feedback form in the docs folder to report any issues. >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo ## Enabling Debug Mode: >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo To enable extended logging, copy NovaHost_Debug.config from the bin folder >> "%PACKAGE_DIR%\README.txt"
echo to the same location as the NovaHost executable. >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo ## Running Performance Tests: >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo The tools folder contains RunPerformanceTests.bat which will run a >> "%PACKAGE_DIR%\README.txt"
echo comprehensive test suite and generate detailed logs. >> "%PACKAGE_DIR%\README.txt"
echo. >> "%PACKAGE_DIR%\README.txt"
echo Thank you for your help in making NovaHost better! >> "%PACKAGE_DIR%\README.txt"

rem Create ZIP archive of the package
echo.
echo Creating ZIP archive...
powershell -command "Compress-Archive -Path '%PACKAGE_DIR%\*' -DestinationPath '%PACKAGE_DIR%.zip' -Force"

echo.
echo Package created successfully!
echo.
echo Package location: %PACKAGE_DIR%
echo ZIP archive: %PACKAGE_DIR%.zip
echo.
echo Distribution package is now ready to send to testers.

:END
pause