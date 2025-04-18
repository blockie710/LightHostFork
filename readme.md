# Light Host

A simple VST/AU/VST3/LADSPA/LV2/AAX/ARA/AU v3 plugin host for macOS, Windows, and Linux that sits in the menu/task bar.

## Features

- Lightweight menu/taskbar application with minimal UI
- Supports VST, VST3, Audio Unit (macOS), Audio Unit v3 (macOS), AAX, ARA, LADSPA, and LV2 plugin formats
- Dynamic plugin chain with bypass capability
- Safe plugin scanning system with blacklisting for problematic plugins
- High DPI display support for Windows 11
- Multi-instance support via command line parameters
- Persistent plugin settings
- Plugin order can be rearranged with drag and drop

## Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)

## Building from Source

### Prerequisites

- [JUCE Framework](https://juce.com/) (automatically included as a local copy)
- C++ development environment:
  - Windows: Visual Studio 2015/2022
  - macOS: Xcode
  - Linux: CodeBlocks or Make

### Compilation Steps

1. Clone this repository
2. Open the `LightHost.jucer` file with Projucer
3. Select your target platform and export the project
4. Build using your platform's development environment

## Command Line Options

- `-multi-instance=NAME`: Run multiple instances with separate settings, where NAME is a unique identifier

## Contributors

- [Rolando Islas](https://github.com/rolandoislas)
- [blockie710](https://github.com/blockie710)

## License

This project is licensed under the GNU General Public License v2 - see the [license](license) file for details.