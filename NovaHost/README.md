# Nova Host

A modern VST/AU/VST3/LADSPA/LV2/AAX/ARA/AU v3 plugin host for macOS, Windows, and Linux that sits in the menu/task bar.

## Features

- **Lightweight Interface**: Lives in your system tray/menu bar for minimal visual footprint
- **Multiple Plugin Formats**: VST, VST3, Audio Unit (macOS), Audio Unit v3 (macOS), LADSPA, LV2, AAX, and ARA plugins
- **Plugin Chain**: Dynamic processing chain with unlimited plugins
- **Enhanced Safety**: Robust plugin scanning with automatic blacklisting for problematic plugins
- **Modern Display Support**: High DPI scaling for Windows 10/11 and modern macOS
- **Multi-instance Capability**: Run multiple instances with separate settings via command-line parameters
- **Persistent Settings**: Plugin states and audio settings are remembered between sessions
- **Rearrangeable Plugins**: Drag and drop interface for reordering plugins in your chain
- **Bypass Options**: Easily bypass individual plugins while keeping them in your chain

## What's New in Nova Host

Nova Host is a fork of the Light Host project with significant enhancements:

- Completely redesigned plugin scanning system that prevents crashes
- Windows 10/11 professional installer with VST directory detection
- Proper high DPI handling on all platforms
- Memory leak fixes and stability improvements
- Enhanced UI with better plugin organization
- Support for the latest plugin formats

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
