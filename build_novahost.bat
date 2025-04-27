@echo off 
echo ============================================= 
echo NovaHost Unified Build System 
echo ============================================= 
echo. 
echo Options: 
echo   1. Debug Build 
echo   2. Release Build 
echo   3. Clean and Rebuild 
echo   4. Run Tests 
echo   5. Package for Distribution 
echo   6. Exit 
echo. 
set /p choice="Enter your choice (1-6): " 
 
if "%%choice%%"=="1" call build\scripts\build_novahost_fixed.bat Debug 
if "%%choice%%"=="2" call build\scripts\build_novahost_fixed.bat Release 
if "%%choice%%"=="3" call build\scripts\build_nova_improved.bat 
if "%%choice%%"=="4" call run_comprehensive_tests.bat 
if "%%choice%%"=="5" call Utilities\PackageForTesters.bat 
if "%%choice%%"=="6" exit /b 
 
echo Build process completed. 
pause 
