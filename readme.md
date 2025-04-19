# Nova Host (formerly Light Host)

A modern VST/AU/VST3/LADSPA/LV2/AAX/ARA/AU v3 plugin host for macOS, Windows, and Linux that sits in the menu/task bar.

## Features

- Lightweight menu/taskbar application with minimal UI
- Supports VST, VST3, Audio Unit (macOS), Audio Unit v3 (macOS), AAX, ARA, LADSPA, and LV2 plugin formats
- Dynamic plugin chain with bypass capability
- Safe plugin scanning system with blacklisting for problematic plugins
- GPU acceleration for improved rendering performance
- High DPI display support for modern operating systems
- Multi-instance support via command line parameters
- Persistent plugin settings
- Plugin order can be rearranged with drag and drop
- Optimized memory and CPU usage

## Screenshot

![Light Host 1.2](http://i.imgur.com/UF9SWfC.jpg)

> Note: The UI has been updated and improved with the new Nova Host branding. See the [NovaHost README](NovaHost/README.md) for more details on the latest version.

## What's New in Nova Host

The project has been renamed from Light Host to Nova Host, reflecting significant improvements:

- Hardware-accelerated GPU rendering for plugin UIs
- Performance optimizations for both CPU and memory usage
- Modern visual design with new icon and interface elements
- Enhanced plugin scanning system with improved stability
- More robust error handling and crash protection
- Support for the latest plugin formats and standards

For more detailed information about Nova Host's features, see the [NovaHost README](NovaHost/README.md).

## Building from Source

### Prerequisites

- [JUCE Framework](https://juce.com/) (automatically included as a local copy)
- C++ development environment:
  - Windows: Visual Studio 2015/2022
  - macOS: Xcode
  - Linux: CodeBlocks or Make

### Compilation Steps

1. Clone this repository
2. Open the `NovaHost.jucer` file with Projucer (or `LightHost.jucer` for the legacy version)
3. Select your target platform and export the project
4. Build using your platform's development environment

## Command Line Options

- `-multi-instance=NAME`: Run multiple instances with separate settings, where NAME is a unique identifier
- `-gpu-acceleration=off`: Disable GPU acceleration at startup (Nova Host only)

## Contributors

- [Rolando Islas](https://github.com/rolandoislas)
- [blockie710](https://github.com/blockie710)

## License

This project is licensed under the GNU General Public License v2 - see the [license](license) file for details.