@echo off
echo NovaHost Comprehensive Test Suite
echo ================================
echo.
echo This script will run tests for performance, stability, and memory leaks
echo.

set LOG_DIR=test_results_%date:~-4,4%%date:~-7,2%%date:~-10,2%_%time:~0,2%%time:~3,2%
set LOG_DIR=%LOG_DIR: =0%
mkdir %LOG_DIR%
echo All test results will be saved to: %LOG_DIR%

echo.
echo 1. Running Memory Usage Test (60 seconds)...
echo.
echo Start time: %time%
echo Running memory test... This will take 60 seconds.
start /wait "Memory Test" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -memory -timeout=60 -output=%LOG_DIR%\memory_test.txt
echo Memory test completed at: %time%

echo.
echo 2. Running Performance Test (60 seconds)...
echo.
echo Start time: %time%
echo Running performance test... This will take 60 seconds.
start /wait "Performance Test" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -performance -timeout=60 -output=%LOG_DIR%\performance_test.txt
echo Performance test completed at: %time%

echo.
echo 3. Running Stability Test (120 seconds)...
echo.
echo Start time: %time%
echo Running stability test... This will take 120 seconds.
start /wait "Stability Test" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -stability -timeout=120 -output=%LOG_DIR%\stability_test.txt
echo Stability test completed at: %time%

echo.
echo 4. Running Plugin Scan Test (60 seconds)...
echo.
echo Start time: %time%
echo Running plugin scan test... This will take 60 seconds.
start /wait "Plugin Scan" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test -scan-plugins -timeout=60 -output=%LOG_DIR%\plugin_scan.txt
echo Plugin scan test completed at: %time%

echo.
echo 5. Running Multiple Instance Test (30 seconds)...
echo.
echo Start time: %time%
echo Launching 3 instances of NovaHost...
start "Instance 1" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -multi-instance=test1 -output=%LOG_DIR%\multi_instance1.txt
start "Instance 2" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -multi-instance=test2 -output=%LOG_DIR%\multi_instance2.txt
start "Instance 3" "Builds\VisualStudio2022\x64\Release\Nova Host.exe" -multi-instance=test3 -output=%LOG_DIR%\multi_instance3.txt
timeout /t 30 /nobreak > nul
echo Multi-instance test completed at: %time%

echo.
echo All tests completed!
echo.
echo Test results have been saved to: %LOG_DIR%
echo.
echo Press any key to view test results...
pause > nul

echo.
echo Test Results
echo ===========
echo.

echo Memory Test Results:
if exist "%LOG_DIR%\memory_test.txt" (
    type "%LOG_DIR%\memory_test.txt"
) else (
    echo No output file found.
)

echo.
echo Performance Test Results:
if exist "%LOG_DIR%\performance_test.txt" (
    type "%LOG_DIR%\performance_test.txt"
) else (
    echo No output file found.
)

echo.
echo Stability Test Results:
if exist "%LOG_DIR%\stability_test.txt" (
    type "%LOG_DIR%\stability_test.txt"
) else (
    echo No output file found.
)

echo.
echo Plugin Scan Test Results:
if exist "%LOG_DIR%\plugin_scan.txt" (
    type "%LOG_DIR%\plugin_scan.txt"
) else (
    echo No output file found.
)

echo.
echo Multi-Instance Test Results:
if exist "%LOG_DIR%\multi_instance1.txt" type "%LOG_DIR%\multi_instance1.txt"
if exist "%LOG_DIR%\multi_instance2.txt" type "%LOG_DIR%\multi_instance2.txt"
if exist "%LOG_DIR%\multi_instance3.txt" type "%LOG_DIR%\multi_instance3.txt"

echo.
echo Tests completed. Press any key to exit...
pause > nul