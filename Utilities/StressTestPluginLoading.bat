@echo off
echo NovaHost Plugin Loading Stress Test
echo ==================================
echo.
echo This script will stress test the plugin loading and unloading functionality
echo by repeatedly loading and closing plugins to identify memory leaks and stability issues.
echo.

set LOG_FILE=plugin_stress_test_%date:~-4,4%%date:~-7,2%%date:~-10,2%_%time:~0,2%%time:~3,2%%time:~6,2%.log
set LOG_FILE=%LOG_FILE: =0%

rem Create logs directory if it doesn't exist
if not exist ".\logs" mkdir logs

rem Check if NovaHost executable exists
if not exist "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo ERROR: NovaHost executable not found.
    echo Please build NovaHost in Release mode first.
    goto END
)

rem Ask for the number of test cycles
set /p CYCLES="Enter number of test cycles (default: 20): "
if "%CYCLES%"=="" set CYCLES=20

rem Ask for pause between cycles in seconds
set /p PAUSE="Enter pause between cycles in seconds (default: 5): "
if "%PAUSE%"=="" set PAUSE=5

echo.
echo Starting stress test with %CYCLES% cycles
echo Results will be logged to: logs\%LOG_FILE%
echo.
echo Test started at: %time% > logs\%LOG_FILE%
echo Test parameters: >> logs\%LOG_FILE%
echo   Cycles: %CYCLES% >> logs\%LOG_FILE%
echo   Pause between cycles: %PAUSE% seconds >> logs\%LOG_FILE%
echo. >> logs\%LOG_FILE%

rem Start the memory monitoring script in a separate window if available
if exist "MonitorMemoryUsage.ps1" (
    echo Starting memory monitoring in a separate window...
    start powershell -NoProfile -ExecutionPolicy Bypass -File "MonitorMemoryUsage.ps1" -Duration %CYCLES% -Interval 1 -OutputFile "plugin_stress_memory_%date:~-4,4%%date:~-7,2%%date:~-10,2%.csv"
    timeout /t 2 /nobreak > nul
)

echo Cycle,StartTime,EndTime,Status,MemoryUsage > logs\%LOG_FILE%.csv

rem Run test cycles
set FAILURES=0
for /L %%i in (1, 1, %CYCLES%) do (
    echo Cycle %%i of %CYCLES%
    echo Cycle %%i of %CYCLES% starting at %time% >> logs\%LOG_FILE%
    
    echo Starting NovaHost with test plugin...
    set START_TIME=%time%
    
    rem Run NovaHost with command to load a specific test plugin
    start "" /wait "..\Builds\VisualStudio2022\x64\Release\Nova Host.exe" -test-plugin -timeout=10 -output=logs\plugin_cycle_%%i.txt
    
    set END_TIME=%time%
    
    if exist logs\plugin_cycle_%%i.txt (
        type logs\plugin_cycle_%%i.txt >> logs\%LOG_FILE%
        
        rem Check for success or failure
        findstr /C:"Test completed successfully" logs\plugin_cycle_%%i.txt > nul
        if not errorlevel 1 (
            echo - Status: Passed
            echo - Status: Passed >> logs\%LOG_FILE%
            set STATUS=Pass
        ) else (
            echo - Status: FAILED
            echo - Status: FAILED >> logs\%LOG_FILE%
            set /a FAILURES+=1
            set STATUS=Fail
        )
        
        rem Extract memory usage
        for /f "tokens=2 delims=:" %%m in ('findstr /C:"Memory usage" logs\plugin_cycle_%%i.txt') do set MEMORY=%%m
        echo - Memory: %MEMORY%
        echo - Memory: %MEMORY% >> logs\%LOG_FILE%
        
        rem Write to CSV
        echo %%i,%START_TIME%,%END_TIME%,%STATUS%,%MEMORY% >> logs\%LOG_FILE%.csv
    ) else (
        echo - Status: ERROR - No output file
        echo - Status: ERROR - No output file >> logs\%LOG_FILE%
        set /a FAILURES+=1
        echo %%i,%START_TIME%,%END_TIME%,Error,Unknown >> logs\%LOG_FILE%.csv
    )
    
    echo. >> logs\%LOG_FILE%
    
    rem Pause before next cycle
    timeout /t %PAUSE% /nobreak > nul
)

echo.
echo Stress test completed.
echo Total cycles: %CYCLES%
echo Failed cycles: %FAILURES%
echo.
echo Test completed at: %time% >> logs\%LOG_FILE%
echo Total cycles: %CYCLES% >> logs\%LOG_FILE%
echo Failed cycles: %FAILURES% >> logs\%LOG_FILE%

if %FAILURES% EQU 0 (
    echo All tests PASSED!
    echo All tests PASSED! >> logs\%LOG_FILE%
) else (
    echo Warning: %FAILURES% cycles failed!
    echo Warning: %FAILURES% cycles failed! >> logs\%LOG_FILE%
)

echo.
echo For detailed results, check logs\%LOG_FILE%
echo For cycle-by-cycle data, check logs\%LOG_FILE%.csv

:END
pause