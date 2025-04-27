# LightHost Windows Installation Guide

This guide will help you build and install LightHost on Windows 10 and Windows 11 systems.

## Prerequisites

1. **Visual Studio 2022** with C++ desktop development workload
   - Download from: [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/)
   
2. **Inno Setup 6** (for creating the installer)
   - Download from: [Inno Setup](https://jrsoftware.org/isdl.php)

## Building the Application

### Step 1: Generate the Visual Studio project files
1. Open the `LightHost.jucer` file with Projucer
2. Click on "Save Project and Open in Visual Studio"
3. This will generate and open the Visual Studio 2022 project

### Step 2: Build the application
1. In Visual Studio, set the build configuration to "Release" and platform to "x64"
2. Build the solution (Build → Build Solution or press F7)
3. The built executable will be in `Builds\VisualStudio2022\x64\Release\`

## Creating the Installer

### Method 1: Using the provided batch file
1. Navigate to the `Installer` directory
2. Run `BuildInstaller.bat`
3. The installer will be created in the `Installer\Output` directory

### Method 2: Manual compilation
1. Install Inno Setup 6
2. Open `Installer\LightHostSetup.iss` in Inno Setup
3. Click Build → Compile
4. The installer will be created in the `Installer\Output` directory

## Installation

Run the `LightHostSetup.exe` installer and follow the on-screen instructions:

1. Accept the license agreement
2. Choose installation location
3. Select VST/VST3 plugin directories (optional)
4. Choose whether to create desktop shortcuts and start on Windows boot
5. Complete the installation

## After Installation

LightHost will appear in your system tray when running. Click on the icon to:
- Configure audio settings
- Manage plugins
- Create plugin chains

## Troubleshooting

1. **Missing JUCE DLLs**: Ensure all required DLLs are in the same folder as the executable
2. **Plugin scanning issues**: Try running as administrator for the first launch
3. **Audio device issues**: Check audio settings by right-clicking the tray icon

## Uninstalling

1. Go to Windows Settings → Apps → Apps & features
2. Find "Light Host" in the list
3. Click Uninstall and follow the prompts

## Building from Source for Windows 7/8
While the installer targets Windows 10/11, the application can be built for older Windows versions by:
1. Modifying the JUCE project to target an older Windows SDK
2. Adjusting the Inno Setup script's `MinVersion` parameter
3. Building using Visual Studio 2015 instead of 2022