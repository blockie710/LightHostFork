@echo off
echo NovaHost Release Build Script (with fixes)
echo =======================================
echo.

rem Set environment variables
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set LOGFILE=build_log_detailed.txt

echo Logging detailed output to %LOGFILE%
echo Build started at %DATE% %TIME% > %LOGFILE%

rem Find Visual Studio installation
echo Detecting Visual Studio installation...
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do (
        set VS_PATH=%%i
        echo Found Visual Studio at: %%i >> %LOGFILE%
    )
) else (
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional
    echo Using default Visual Studio path: %VS_PATH% >> %LOGFILE%
)

rem Set MSBuild path
set MSBUILD_PATH="%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
echo Using MSBuild at: %MSBUILD_PATH% >> %LOGFILE%

rem Check if MSBuild exists
if not exist %MSBUILD_PATH% (
    echo MSBuild not found at: %MSBUILD_PATH%
    echo Looking for MSBuild in alternative locations...
    
    rem Try to find MSBuild elsewhere
    for /f "tokens=*" %%i in ('dir /s /b "%VS_PATH%\MSBuild.exe" 2^>nul') do (
        set MSBUILD_PATH="%%i"
        echo Found MSBuild at: %%i >> %LOGFILE%
        goto MSBuildFound
    )
    
    echo ERROR: MSBuild not found! >> %LOGFILE%
    echo ERROR: MSBuild not found!
    goto ERROR
)

:MSBuildFound
echo MSBuild path: %MSBUILD_PATH%

rem Create build directories if they don't exist
echo Creating build directories...
if not exist "Builds" mkdir "Builds"
if not exist "Builds\VisualStudio2022" mkdir "Builds\VisualStudio2022"
if not exist "Builds\VisualStudio2022\x64" mkdir "Builds\VisualStudio2022\x64"
if not exist "Builds\VisualStudio2022\x64\Release" mkdir "Builds\VisualStudio2022\x64\Release"

rem Fix the NovaHost.vcxproj file
echo.
echo Backing up and fixing NovaHost.vcxproj file...
copy NovaHost.vcxproj NovaHost.vcxproj.backup
echo Original project file backed up to NovaHost.vcxproj.backup >> %LOGFILE%

rem Create a temporary file with fixed project structure
set TEMP_PROJECT=NovaHost.vcxproj.fixed
echo Creating temporary project file: %TEMP_PROJECT%

rem Using a proper method to create XML file
echo ^<?xml version="1.0" encoding="utf-8"?^> > %TEMP_PROJECT%
echo ^<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"^> >> %TEMP_PROJECT%
echo   ^<ItemGroup Label="ProjectConfigurations"^> >> %TEMP_PROJECT%
echo     ^<ProjectConfiguration Include="Debug|x64"^> >> %TEMP_PROJECT%
echo       ^<Configuration^>Debug^</Configuration^> >> %TEMP_PROJECT%
echo       ^<Platform^>x64^</Platform^> >> %TEMP_PROJECT%
echo     ^</ProjectConfiguration^> >> %TEMP_PROJECT%
echo     ^<ProjectConfiguration Include="Release|x64"^> >> %TEMP_PROJECT%
echo       ^<Configuration^>Release^</Configuration^> >> %TEMP_PROJECT%
echo       ^<Platform^>x64^</Platform^> >> %TEMP_PROJECT%
echo     ^</ProjectConfiguration^> >> %TEMP_PROJECT%
echo   ^</ItemGroup^> >> %TEMP_PROJECT%
echo   ^<PropertyGroup Label="Globals"^> >> %TEMP_PROJECT%
echo     ^<VCProjectVersion^>17.0^</VCProjectVersion^> >> %TEMP_PROJECT%
echo     ^<ProjectGuid^>{0946F518-1A24-54EF-65AD-C90AE0521D03}^</ProjectGuid^> >> %TEMP_PROJECT%
echo     ^<Keyword^>ManagedCProj^</Keyword^> >> %TEMP_PROJECT%
echo     ^<ProjectName^>NovaHost^</ProjectName^> >> %TEMP_PROJECT%
echo     ^<WindowsTargetPlatformVersion^>10.0^</WindowsTargetPlatformVersion^> >> %TEMP_PROJECT%
echo   ^</PropertyGroup^> >> %TEMP_PROJECT%
echo   ^<Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" /^> >> %TEMP_PROJECT%
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration"^> >> %TEMP_PROJECT%
echo     ^<ConfigurationType^>Application^</ConfigurationType^> >> %TEMP_PROJECT%
echo     ^<UseDebugLibraries^>true^</UseDebugLibraries^> >> %TEMP_PROJECT%
echo     ^<PlatformToolset^>v143^</PlatformToolset^> >> %TEMP_PROJECT%
echo     ^<CharacterSet^>Unicode^</CharacterSet^> >> %TEMP_PROJECT%
echo   ^</PropertyGroup^> >> %TEMP_PROJECT%
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration"^> >> %TEMP_PROJECT%
echo     ^<ConfigurationType^>Application^</ConfigurationType^> >> %TEMP_PROJECT%
echo     ^<UseDebugLibraries^>false^</UseDebugLibraries^> >> %TEMP_PROJECT%
echo     ^<PlatformToolset^>v143^</PlatformToolset^> >> %TEMP_PROJECT%
echo     ^<WholeProgramOptimization^>true^</WholeProgramOptimization^> >> %TEMP_PROJECT%
echo     ^<CharacterSet^>Unicode^</CharacterSet^> >> %TEMP_PROJECT%
echo   ^</PropertyGroup^> >> %TEMP_PROJECT%
echo   ^<Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" /^> >> %TEMP_PROJECT%
echo   ^<ImportGroup Label="ExtensionSettings"^> >> %TEMP_PROJECT%
echo   ^</ImportGroup^> >> %TEMP_PROJECT%
echo   ^<ImportGroup Label="Shared"^> >> %TEMP_PROJECT%
echo   ^</ImportGroup^> >> %TEMP_PROJECT%
echo   ^<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'"^> >> %TEMP_PROJECT%
echo     ^<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" /^> >> %TEMP_PROJECT%
echo   ^</ImportGroup^> >> %TEMP_PROJECT%
echo   ^<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'"^> >> %TEMP_PROJECT%
echo     ^<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" /^> >> %TEMP_PROJECT%
echo   ^</ImportGroup^> >> %TEMP_PROJECT%
echo   ^<PropertyGroup Label="UserMacros" /^> >> %TEMP_PROJECT%
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'"^> >> %TEMP_PROJECT%
echo     ^<OutDir^>$(SolutionDir)Builds\VisualStudio2022\$(Platform)\$(Configuration)\^</OutDir^> >> %TEMP_PROJECT%
echo     ^<TargetName^>Nova Host^</TargetName^> >> %TEMP_PROJECT%
echo   ^</PropertyGroup^> >> %TEMP_PROJECT%
echo   ^<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'"^> >> %TEMP_PROJECT%
echo     ^<ClCompile^> >> %TEMP_PROJECT%
echo       ^<WarningLevel^>Level3^</WarningLevel^> >> %TEMP_PROJECT%
echo       ^<FunctionLevelLinking^>true^</FunctionLevelLinking^> >> %TEMP_PROJECT%
echo       ^<IntrinsicFunctions^>true^</IntrinsicFunctions^> >> %TEMP_PROJECT%
echo       ^<SDLCheck^>true^</SDLCheck^> >> %TEMP_PROJECT%
echo       ^<PreprocessorDefinitions^>NDEBUG;_CONSOLE;%(PreprocessorDefinitions)^</PreprocessorDefinitions^> >> %TEMP_PROJECT%
echo       ^<ConformanceMode^>true^</ConformanceMode^> >> %TEMP_PROJECT%
echo     ^</ClCompile^> >> %TEMP_PROJECT%
echo     ^<Link^> >> %TEMP_PROJECT%
echo       ^<EnableCOMDATFolding^>true^</EnableCOMDATFolding^> >> %TEMP_PROJECT%
echo       ^<OptimizeReferences^>true^</OptimizeReferences^> >> %TEMP_PROJECT%
echo       ^<GenerateDebugInformation^>true^</GenerateDebugInformation^> >> %TEMP_PROJECT%
echo       ^<SubSystem^>Windows^</SubSystem^> >> %TEMP_PROJECT%
echo     ^</Link^> >> %TEMP_PROJECT%
echo   ^</ItemDefinitionGroup^> >> %TEMP_PROJECT%
echo   ^<ItemGroup^> >> %TEMP_PROJECT%
echo     ^<ClCompile Include="Source\HostStartup.cpp" /^> >> %TEMP_PROJECT%
echo     ^<ClCompile Include="Source\IconMenu.cpp" /^> >> %TEMP_PROJECT%
echo     ^<ClCompile Include="Source\PluginWindow.cpp" /^> >> %TEMP_PROJECT%
echo   ^</ItemGroup^> >> %TEMP_PROJECT%
echo   ^<ItemGroup^> >> %TEMP_PROJECT%
echo     ^<ClInclude Include="Source\IconMenu.hpp" /^> >> %TEMP_PROJECT%
echo     ^<ClInclude Include="Source\PluginWindow.h" /^> >> %TEMP_PROJECT%
echo   ^</ItemGroup^> >> %TEMP_PROJECT%
echo   ^<Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" /^> >> %TEMP_PROJECT%
echo   ^<ImportGroup Label="ExtensionTargets"^> >> %TEMP_PROJECT%
echo   ^</ImportGroup^> >> %TEMP_PROJECT%
echo ^</Project^> >> %TEMP_PROJECT%

rem Replace the original project file with our fixed version
move /y %TEMP_PROJECT% NovaHost.vcxproj > nul
echo Fixed project file created >> %LOGFILE%

rem Now try to build
echo.
echo Building NovaHost in Release mode...
%MSBUILD_PATH% NovaHost.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /fl /flp:logfile=%LOGFILE%;verbosity=detailed

if %ERRORLEVEL% NEQ 0 (
    echo Build failed with error code: %ERRORLEVEL%
    echo See %LOGFILE% for detailed error information
    goto ERROR
)

rem Check if build succeeded by looking for the executable
if exist "Builds\VisualStudio2022\x64\Release\Nova Host.exe" (
    echo Build successful! NovaHost executable created at:
    echo Builds\VisualStudio2022\x64\Release\Nova Host.exe
    
    echo.
    echo You can now run the PackageForTesters.bat script in the Utilities folder
    echo to create a distribution package for testing.
) else (
    echo Build process completed but executable not found at the expected location.
    echo This may indicate that the build output path is different.
    
    echo.
    echo Searching for Nova Host.exe in the Builds directory...
    for /f "tokens=*" %%i in ('dir /s /b "Builds\*Nova Host.exe" 2^>nul') do (
        echo Found executable at: %%i
    )
)

goto END

:ERROR
echo.
echo Build process failed. 
echo Please check the log file: %LOGFILE%
echo.
exit /b 1

:END
echo.
echo Build process completed.
echo.
pause