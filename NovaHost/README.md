# Nova Host

A modern VST/AU/VST3/LADSPA/LV2/AAX/ARA/AU v3 plugin host for macOS, Windows, and Linux that sits in the menu/task bar.

Nova Host is a fork of the Light Host project, enhanced with improved Windows support, a more robust plugin scanning system, and other features.

## Features

- Lightweight menu/taskbar application with minimal UI
- Supports VST, VST3, Audio Unit (macOS), Audio Unit v3 (macOS), AAX, ARA, LADSPA, and LV2 plugin formats
- Dynamic plugin chain with bypass capability
- Safe plugin scanning system with blacklisting for problematic plugins
- High DPI display support for Windows 10/11
- Multi-instance support via command line parameters
- Persistent plugin settings
- Plugin order can be rearranged with drag and drop

## Building from Source

### Prerequisites

- [JUCE Framework](https://juce.com/) (automatically included as a local copy)
- C++ development environment:
  - Windows: Visual Studio 2022 (recommended) or 2015
  - macOS: Xcode
  - Linux: CodeBlocks or Make

### Compilation Steps

1. Clone this repository
2. Open the `NovaHost.jucer` file with Projucer
3. Select your target platform and export the project
4. Build using your platform's development environment

#### For Windows Users
See the detailed [Windows Installation Guide](WINDOWS_INSTALL_GUIDE.md) for specific instructions on building and creating the installer.

## Command Line Options

- `-multi-instance=NAME`: Run multiple instances with separate settings, where NAME is a unique identifier

## License

This project is licensed under the GNU General Public License v2 - see the [license](../license) file for details.