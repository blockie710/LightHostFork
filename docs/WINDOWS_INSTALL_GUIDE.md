# NovaHost Windows Installation Guide

This guide will help you build and install NovaHost (formerly LightHostFork) on Windows systems.

## Prerequisites

1. **Visual Studio 2022** with C++ desktop development workload
   - Download from: [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/)
   - Required components:
     - C++ core desktop features
     - MSVC C++ build tools
     - Windows 10/11 SDK
   
2. **Inno Setup 6** (for creating the installer)
   - Download from: [Inno Setup](https://jrsoftware.org/isdl.php)
   - Version 6.0.0 or newer is required
   
3. **Git** (optional, for cloning the repository)
   - Download from: [Git for Windows](https://git-scm.com/download/win)

## Building the Application

### Step 1: Obtaining the source code
1. Clone the repository using Git:
   ```
   git clone https://github.com/blockie710/NovaHost.git
   ```
   
   Or download and extract the ZIP archive from GitHub.

### Step 2: Generate the Visual Studio project files
1. Navigate to the NovaHost directory
2. Open the `NovaHost.jucer` file with Projucer
   - If Projucer is not available, it can be built from the JUCE repository or downloaded from [JUCE](https://juce.com/get-juce/)
3. Click on "Save Project and Open in Visual Studio"
4. This will generate and open the Visual Studio 2022 project

### Step 3: Build the application
1. In Visual Studio, set the build configuration to "Release" and platform to "x64"
2. Right-click on the NovaHost project in Solution Explorer and select "Set as StartUp Project"
3. Build the solution (Build → Build Solution or press F7)
4. The built executable will be in `Builds\VisualStudio2022\x64\Release\NovaHost.exe`
5. Verify the build was successful by running the executable directly

## Creating the Installer

### Method 1: Using the provided batch file (Recommended)
1. Navigate to the `Installer` directory
2. Run `BuildNovaHostInstaller.bat` by double-clicking it
   - Note: This batch file assumes Inno Setup is installed in the default location
   - If installed elsewhere, edit the batch file to point to your Inno Setup compiler
3. Wait for the compilation to complete
4. The installer will be created in the `Installer\Output` directory as `NovaHostSetup-x.x.x.exe` (where x.x.x is the version number)

### Method 2: Manual compilation
1. Install Inno Setup 6
2. Open `Installer\NovaHostSetup.iss` in Inno Setup Compiler
3. Review and update paths if necessary (if your build locations differ from defaults)
4. Click Build → Compile (or press Ctrl+F9)
5. The installer will be created in the `Installer\Output` directory

## Installation

Run the `NovaHostSetup-x.x.x.exe` installer and follow the on-screen instructions:

1. Accept the license agreement
2. Choose installation location
   - Default: `C:\Program Files\NovaHost`
   - User-specific installations can be placed in `%LOCALAPPDATA%\Programs\NovaHost`
3. Select VST/VST3 plugin directories (optional)
   - Common VST paths:
     - `C:\Program Files\Common Files\VST3`
     - `C:\Program Files\VSTPlugins`
     - `C:\Program Files\Steinberg\VSTPlugins`
4. Choose whether to create desktop shortcuts and start NovaHost on Windows boot
5. Complete the installation

## After Installation

NovaHost will appear in your system tray when running. Click on the icon to:
- Configure audio settings (audio device, buffer size, sample rate)
- Manage plugins (scan, blacklist, organize)
- Create plugin chains (load, save, edit)
- Access plugin windows and parameters

### First-time Configuration
1. Right-click the NovaHost icon in the system tray
2. Select "Settings" to configure audio device settings
3. Choose "Scan for Plugins" to detect available plugins
4. Wait for the scanning process to complete
5. Create your first plugin chain by clicking "Open Plugin"

## Troubleshooting

### Common Issues and Solutions

1. **Missing JUCE DLLs**:
   - Ensure all required DLLs are in the same folder as the executable
   - If missing, copy them from `Builds\VisualStudio2022\x64\Release\`
   
2. **Plugin scanning issues**:
   - Try running NovaHost as administrator for the first launch
   - Verify plugin directories are correctly set in Settings
   - Check if plugins require additional dependencies
   
3. **Audio device initialization failed**:
   - Verify the selected audio device is connected and functioning
   - Try a different audio driver type (ASIO, DirectSound, WASAPI)
   - Adjust buffer size (larger values are more stable but have higher latency)

4. **High CPU usage or audio glitches**:
   - Increase audio buffer size in settings
   - Close other audio applications
   - Check for problematic plugins and consider blacklisting them

5. **Installation fails**:
   - Ensure you have administrator privileges
   - Temporarily disable antivirus software
   - Check Windows Event Viewer for detailed error messages

## Advanced Configuration

### Command Line Options
NovaHost supports several command-line parameters:
- `-multi-instance=NAME`: Run multiple instances with separate settings
- `-plugins=PATH`: Specify a custom plugins folder
- `-settings=PATH`: Use a specific settings file
- `-debug`: Enable debug output

### Registry Settings
Settings are stored in:
- `HKEY_CURRENT_USER\Software\NovaHost` (for user installations)
- Plugin blacklist, audio device preferences, and UI settings can be found here

## Uninstalling

### Method 1: Windows Settings
1. Go to Windows Settings → Apps → Apps & features
2. Find "NovaHost" in the list
3. Click Uninstall and follow the prompts

### Method 2: Control Panel
1. Open Control Panel → Programs → Programs and Features
2. Select "NovaHost" from the list
3. Click Uninstall and follow the prompts

### Method 3: Original Installer
1. Run the original `NovaHostSetup-x.x.x.exe` installer
2. Select "Remove NovaHost" option

### Manual Cleanup (if needed)
After uninstalling, you may want to remove:
1. User settings: Delete `%APPDATA%\NovaHost` folder
2. Registry entries: Delete `HKEY_CURRENT_USER\Software\NovaHost` using Registry Editor

## Building for Different Windows Versions

### Windows 7/8 Support
To build for older Windows versions:
1. In Visual Studio, open project properties
2. Set Platform Toolset to v141 or earlier
3. Set Windows SDK Version to 10.0.17763.0 or earlier
4. Rebuild the solution

## Updating from Previous Versions

If updating from LightHostFork or an older version of NovaHost:
1. Uninstall the previous version first
2. Install the new version
3. Your existing settings will be migrated automatically