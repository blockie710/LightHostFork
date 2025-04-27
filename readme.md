# Light Host Fork

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

#### Windows-Specific Instructions
For detailed instructions on building and installing on Windows, see [Windows Installation Guide](Installer/WINDOWS_INSTALL_GUIDE.md).

## Testing

### Running Tests

1. Open the solution in Visual Studio
2. Select the "Testing" configuration from the configuration dropdown
3. Build and run the solution to execute all unit tests

### Writing Tests

Tests are located in the `Tests/` directory and use JUCE's built-in unit testing framework. To add new tests:

1. Create a new test class by extending `UnitTest`
2. Implement the `runTest()` method with your test cases
3. Register your test class using a static instance

Example:
```cpp
class MyNewTests : public UnitTest
{
public:
    MyNewTests() : UnitTest("My New Test Suite") {}
    
    void runTest() override
    {
        beginTest("Test Case Description");
        expect(myCondition, "Expected condition description");
    }
};

static MyNewTests myNewTests;
```

## Command Line Options

- `-multi-instance=NAME`: Run multiple instances with separate settings, where NAME is a unique identifier

## Project Status and Roadmap

LightHostFork is under active development. We maintain a detailed task list with prioritized improvements:

- 🔴 **High Priority**: Memory management, thread safety, and error handling issues
- 🟠 **Medium Priority**: Code organization and style consistency tasks
- 🟡 **Lower Priority**: C++ modernization and UX improvements

See the [CONTRIBUTING.md](CONTRIBUTING.md) file for the complete task list and development roadmap.

## Contributing

Contributions are welcome! Please check the [CONTRIBUTING.md](CONTRIBUTING.md) file for:

- Detailed task list and roadmap
- Contribution guidelines
- Code style recommendations
- Development setup instructions

## Related Projects

- [NovaHost](NovaHost/README.md): An enhanced fork with improved Windows support and additional features

## Contributors

- [Rolando Islas](https://github.com/rolandoislas)
- [blockie710](https://github.com/blockie710)

## License

This project is licensed under the GNU General Public License v2 - see the [license](license) file for details.