@echo off
echo NovaHost Performance & Stability Test Suite
echo ========================================
echo.
echo This script will run comprehensive performance and stability tests
echo to ensure NovaHost is ready for distribution to testers.
echo.

set LOG_FILE=NovaHost_TestResults_%date:~-4,4%%date:~-7,2%%date:~-10,2%_%time:~0,2%%time:~3,2%%time:~6,2%.log
set LOG_FILE=%LOG_FILE: =0%
echo All test results will be logged to: %LOG_FILE%
echo.

rem Create logs directory if it doesn't exist
if not exist ".\logs" mkdir logs

echo Starting tests at %time% > logs\%LOG_FILE%
echo System Information: >> logs\%LOG_FILE%
systeminfo | findstr /B /C:"OS Name" /C:"OS Version" /C:"System Manufacturer" /C:"System Model" /C:"Processor" /C:"Total Physical Memory" >> logs\%LOG_FILE%
echo. >> logs\%LOG_FILE%

echo 1. Build Verification Test
echo -------------------------
echo Testing NovaHost build...
echo. 

echo [1/6] Build Verification Test >> logs\%LOG_FILE%
echo Running at %time% >> logs\%LOG_FILE%

rem Check if NovaHost executable exists
if exist "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo Build exists - PASSED
    echo Build verification: PASSED >> logs\%LOG_FILE%
) else (
    echo Build not found - FAILED
    echo Build verification: FAILED - Executable not found >> logs\%LOG_FILE%
    echo Please build NovaHost in Release mode before running tests.
    goto END
)

echo.
echo 2. Load Test - Basic Functionality
echo --------------------------------
echo Testing basic functionality...
echo.

echo [2/6] Load Test - Basic Functionality >> logs\%LOG_FILE%
echo Running at %time% >> logs\%LOG_FILE%

rem Run NovaHost in test mode for 30 seconds
echo Starting NovaHost in test mode...
start "" /B "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -timeout=30 -output=logs\basic_test_output.txt
echo Waiting for test to complete (30 seconds)...
timeout /t 30 /nobreak > nul

if exist "logs\basic_test_output.txt" (
    echo Basic functionality test results: >> logs\%LOG_FILE%
    type logs\basic_test_output.txt >> logs\%LOG_FILE%
    
    findstr /C:"Test passed" logs\basic_test_output.txt > nul
    if not errorlevel 1 (
        echo Basic functionality test - PASSED
        echo Basic functionality: PASSED >> logs\%LOG_FILE%
    ) else (
        echo Basic functionality test - FAILED
        echo Basic functionality: FAILED >> logs\%LOG_FILE%
    )
) else (
    echo Basic functionality test output not found - FAILED
    echo Basic functionality: FAILED - No output file >> logs\%LOG_FILE%
)

echo.
echo 3. Memory Usage Test
echo ------------------
echo Testing for memory leaks...
echo.

echo [3/6] Memory Usage Test >> logs\%LOG_FILE%
echo Running at %time% >> logs\%LOG_FILE%

echo Starting NovaHost for memory testing...
start "" /B "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -memory -timeout=60 -output=logs\memory_test_output.txt
echo Waiting for memory test to complete (60 seconds)...
timeout /t 60 /nobreak > nul

if exist "logs\memory_test_output.txt" (
    echo Memory test results: >> logs\%LOG_FILE%
    type logs\memory_test_output.txt >> logs\%LOG_FILE%
    
    findstr /C:"Memory test passed" logs\memory_test_output.txt > nul
    if not errorlevel 1 (
        echo Memory test - PASSED
        echo Memory test: PASSED >> logs\%LOG_FILE%
    ) else (
        echo Memory test - FAILED
        echo Memory test: FAILED >> logs\%LOG_FILE%
    )
) else (
    echo Memory test output not found - FAILED
    echo Memory test: FAILED - No output file >> logs\%LOG_FILE%
)

echo.
echo 4. Plugin Scanning Test
echo ---------------------
echo Testing plugin scanning performance...
echo.

echo [4/6] Plugin Scanning Test >> logs\%LOG_FILE%
echo Running at %time% >> logs\%LOG_FILE%

echo Starting NovaHost for plugin scanning test...
start "" /B "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -scan-plugins -timeout=120 -output=logs\plugin_scan_output.txt
echo Waiting for plugin scan test to complete (may take 2 minutes)...
timeout /t 120 /nobreak > nul

if exist "logs\plugin_scan_output.txt" (
    echo Plugin scan test results: >> logs\%LOG_FILE%
    type logs\plugin_scan_output.txt >> logs\%LOG_FILE%
    
    findstr /C:"Plugin scan completed successfully" logs\plugin_scan_output.txt > nul
    if not errorlevel 1 (
        echo Plugin scanning test - PASSED
        echo Plugin scanning test: PASSED >> logs\%LOG_FILE%
    ) else (
        echo Plugin scanning test - FAILED
        echo Plugin scanning test: FAILED >> logs\%LOG_FILE%
    )
) else (
    echo Plugin scan test output not found - FAILED
    echo Plugin scan test: FAILED - No output file >> logs\%LOG_FILE%
)

echo.
echo 5. Multi-Instance Test
echo --------------------
echo Testing multiple instances...
echo.

echo [5/6] Multi-Instance Test >> logs\%LOG_FILE%
echo Running at %time% >> logs\%LOG_FILE%

echo Starting 3 instances of NovaHost...
start "" /B "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -multi-instance=test1 -output=logs\multi_instance1.txt
start "" /B "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -multi-instance=test2 -output=logs\multi_instance2.txt
start "" /B "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -multi-instance=test3 -output=logs\multi_instance3.txt
echo Waiting for multi-instance test to complete (30 seconds)...
timeout /t 30 /nobreak > nul

echo Checking multi-instance test results...
set MULTI_INSTANCE_PASSED=1

if not exist "logs\multi_instance1.txt" set MULTI_INSTANCE_PASSED=0
if not exist "logs\multi_instance2.txt" set MULTI_INSTANCE_PASSED=0
if not exist "logs\multi_instance3.txt" set MULTI_INSTANCE_PASSED=0

echo Multi-instance test results: >> logs\%LOG_FILE%
if exist "logs\multi_instance1.txt" type logs\multi_instance1.txt >> logs\%LOG_FILE%
if exist "logs\multi_instance2.txt" type logs\multi_instance2.txt >> logs\%LOG_FILE%
if exist "logs\multi_instance3.txt" type logs\multi_instance3.txt >> logs\%LOG_FILE%

if "%MULTI_INSTANCE_PASSED%"=="1" (
    echo Multi-instance test - PASSED
    echo Multi-instance test: PASSED >> logs\%LOG_FILE%
) else (
    echo Multi-instance test - FAILED
    echo Multi-instance test: FAILED >> logs\%LOG_FILE%
)

echo.
echo 6. Unit Tests
echo -----------
echo Running unit tests...
echo.

echo [6/6] Unit Tests >> logs\%LOG_FILE%
echo Running at %time% >> logs\%LOG_FILE%

echo Building and running unit tests...
echo Please select "Testing" configuration and run unit tests from Visual Studio.
echo OR use the command line method below:
echo.
echo Command: "..\Builds\VisualStudio2022\x64\Testing\NovaHostTests.exe" --gtest_output=xml:logs\unit_test_results.xml
echo.
echo NOTE: After running unit tests, press any key to continue...
pause > nul

if exist "logs\unit_test_results.xml" (
    echo Unit test results: >> logs\%LOG_FILE%
    type logs\unit_test_results.xml >> logs\%LOG_FILE%
    
    echo Unit tests were run - check logs\unit_test_results.xml for details
    echo Unit tests completed >> logs\%LOG_FILE%
) else (
    echo Unit test results not found
    echo Unit tests: RESULTS NOT FOUND >> logs\%LOG_FILE%
)

echo.
echo All tests completed!
echo Test summary: >> logs\%LOG_FILE%
echo Tests completed at %time% >> logs\%LOG_FILE%
echo.
echo For detailed results, check: logs\%LOG_FILE%
echo.

:END
echo Press any key to exit...
pause > nul