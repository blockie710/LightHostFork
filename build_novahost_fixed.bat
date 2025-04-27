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

echo Adding proper project configurations to NovaHost.vcxproj...
(
echo ^<?xml version="1.0" encoding="utf-8"?^>
echo ^<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"^>
echo   ^<ItemGroup Label="ProjectConfigurations"^>
echo     ^<ProjectConfiguration Include="Debug|x64"^>
echo       ^<Configuration^>Debug^</Configuration^>
echo       ^<Platform^>x64^</Platform^>
echo     ^</ProjectConfiguration^>
echo     ^<ProjectConfiguration Include="Release|x64"^>
echo       ^<Configuration^>Release^</Configuration^>
echo       ^<Platform^>x64^</Platform^>
echo     ^</ProjectConfiguration^>
echo     ^<ProjectConfiguration Include="Debug|Win32"^>
echo       ^<Configuration^>Debug^</Configuration^>
echo       ^<Platform^>Win32^</Platform^>
echo     ^</ProjectConfiguration^>
echo     ^<ProjectConfiguration Include="Release|Win32"^>
echo       ^<Configuration^>Release^</Configuration^>
echo       ^<Platform^>Win32^</Platform^>
echo     ^</ProjectConfiguration^>
echo   ^</ItemGroup^>
echo   ^<PropertyGroup Label="Globals"^>
echo     ^<VCProjectVersion^>17.0^</VCProjectVersion^>
echo     ^<ProjectGuid^>{0946F518-1A24-54EF-65AD-C90AE0521D03}^</ProjectGuid^>
echo     ^<Keyword^>ManagedCProj^</Keyword^>
echo     ^<ProjectName^>NovaHost^</ProjectName^>
echo     ^<WindowsTargetPlatformVersion^>10.0^</WindowsTargetPlatformVersion^>
echo   ^</PropertyGroup^>
echo   ^<Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" /^>
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration"^>
echo     ^<ConfigurationType^>Application^</ConfigurationType^>
echo     ^<UseDebugLibraries^>true^</UseDebugLibraries^>
echo     ^<PlatformToolset^>v143^</PlatformToolset^>
echo     ^<CharacterSet^>Unicode^</CharacterSet^>
echo   ^</PropertyGroup^>
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration"^>
echo     ^<ConfigurationType^>Application^</ConfigurationType^>
echo     ^<UseDebugLibraries^>false^</UseDebugLibraries^>
echo     ^<PlatformToolset^>v143^</PlatformToolset^>
echo     ^<WholeProgramOptimization^>true^</WholeProgramOptimization^>
echo     ^<CharacterSet^>Unicode^</CharacterSet^>
echo   ^</PropertyGroup^>
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="Configuration"^>
echo     ^<ConfigurationType^>Application^</ConfigurationType^>
echo     ^<UseDebugLibraries^>true^</UseDebugLibraries^>
echo     ^<PlatformToolset^>v143^</PlatformToolset^>
echo     ^<CharacterSet^>Unicode^</CharacterSet^>
echo   ^</PropertyGroup^>
echo   ^<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'" Label="Configuration"^>
echo     ^<ConfigurationType^>Application^</ConfigurationType^>
echo     ^<UseDebugLibraries^>false^</UseDebugLibraries^>
echo     ^<PlatformToolset^>v143^</PlatformToolset^>
echo     ^<WholeProgramOptimization^>true^</WholeProgramOptimization^>
echo     ^<CharacterSet^>Unicode^</CharacterSet^>
echo   ^</PropertyGroup^>
echo   ^<Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" /^>
echo   ^<ImportGroup Label="ExtensionSettings"^>
echo   ^</ImportGroup^>
echo   ^<ImportGroup Label="Shared"^>
echo   ^</ImportGroup^>
echo   ^<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'"^>
echo     ^<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" /^>
echo   ^</ImportGroup^>
echo   ^<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|Win32'"^>
echo     ^<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" /^>
echo   ^</ImportGroup^>
echo   ^<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'"^>
echo     ^<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" /^>
echo   ^</ImportGroup^>
echo   ^<ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'"^>
echo     ^<Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" /^>
echo   ^</ImportGroup^>
echo   ^<PropertyGroup Label="UserMacros" /^>
echo   ^<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'"^>
echo     ^<ClCompile^>
echo       ^<WarningLevel^>Level3^</WarningLevel^>
echo       ^<FunctionLevelLinking^>true^</FunctionLevelLinking^>
echo       ^<IntrinsicFunctions^>true^</IntrinsicFunctions^>
echo       ^<SDLCheck^>true^</SDLCheck^>
echo       ^<PreprocessorDefinitions^>NDEBUG;_CONSOLE;%(PreprocessorDefinitions)^</PreprocessorDefinitions^>
echo       ^<ConformanceMode^>true^</ConformanceMode^>
echo     ^</ClCompile^>
echo     ^<Link^>
echo       ^<EnableCOMDATFolding^>true^</EnableCOMDATFolding^>
echo       ^<OptimizeReferences^>true^</OptimizeReferences^>
echo       ^<GenerateDebugInformation^>true^</GenerateDebugInformation^>
echo       ^<SubSystem^>Windows^</SubSystem^>
echo     ^</Link^>
echo   ^</ItemDefinitionGroup^>
echo   ^<ItemGroup^>
echo     ^<ClCompile Include="Source\HostStartup.cpp" /^>
echo     ^<ClCompile Include="Source\IconMenu.cpp" /^>
echo     ^<ClCompile Include="Source\PluginWindow.cpp" /^>
echo   ^</ItemGroup^>
echo   ^<ItemGroup^>
echo     ^<ClInclude Include="Source\IconMenu.hpp" /^>
echo     ^<ClInclude Include="Source\PluginWindow.h" /^>
echo   ^</ItemGroup^>
echo   ^<Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" /^>
echo   ^<ImportGroup Label="ExtensionTargets"^>
echo   ^</ImportGroup^>
echo ^</Project^>
) > NovaHost.vcxproj

echo Fixed project file created >> %LOGFILE%

rem Now try to build
echo.
echo Building NovaHost in Release mode...
%MSBUILD_PATH% NovaHost.sln /p:Configuration=Release /p:Platform=x64 /v:detailed >> %LOGFILE% 2>&1

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