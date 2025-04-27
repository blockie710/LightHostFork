# NovaHost

A modern VST/AU/VST3/LADSPA/LV2/AAX/ARA/AU v3 plugin host for macOS, Windows, and Linux that sits in the menu/task bar.

## Features

- Lightweight menu/taskbar application with minimal UI
- Supports VST, VST3, Audio Unit (macOS), Audio Unit v3 (macOS), AAX, ARA, LADSPA, and LV2 plugin formats
- Dynamic plugin chain with bypass capability
- Safe plugin scanning system with blacklisting for problematic plugins
- High DPI display support for Windows 11
- Multi-instance support via command line parameters
- Persistent plugin settings
- Plugin order can be rearranged with drag and drop
- Cross-platform compatibility (Windows, macOS, Linux)

## Screenshot

![NovaHost 1.2](http://i.imgur.com/UF9SWfC.jpg)

## Project Organization

NovaHost follows a structured organization:

```
NovaHost/
├── build/               # Build artifacts and generated files
├── docs/                # Documentation files
├── Installer/           # Installer scripts and resources
├── Resources/           # Application resources (icons, images)
├── Source/              # Main application source code
│   ├── Core/            # Core application functionality
│   ├── UI/              # User interface components
│   ├── Plugin/          # Plugin management code
│   └── Config/          # Configuration handling
├── Tests/               # Test files
├── third_party/         # Third-party libraries
└── Utilities/           # Utility scripts
```

For more details on the project organization, see [Project Organization Guide](docs/PROJECT_ORGANIZATION.md) and [Implementation Guide](docs/IMPLEMENTATION_GUIDE.md).

## Building from Source

### Prerequisites

- [JUCE Framework](https://juce.com/) (automatically included as a local copy)
- C++ development environment:
  - Windows: Visual Studio 2019/2022 (with C++ Desktop Development workload)
  - macOS: Xcode 12+ with Command Line Tools
  - Linux: GCC 9+ or Clang 10+, CodeBlocks or Make

### Compilation Steps

1. Clone this repository: `git clone https://github.com/blockie710/NovaHost.git`
2. Open the `NovaHost.jucer` file with Projucer
3. Select your target platform and export the project
4. Build using your platform's development environment

#### Windows-Specific Instructions
For detailed instructions on building and installing on Windows, see [Windows Installation Guide](docs/WINDOWS_INSTALL_GUIDE.md).

#### macOS and Linux
Additional platform-specific build instructions can be found in the documentation folder.

## Testing

### Running Tests

1. Open the solution in your development environment
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
- `-plugins=PATH`: Specify a custom plugins folder path
- `-settings=PATH`: Specify a custom settings file path
- `-debug`: Enable additional debug output

## Project Status and Roadmap

NovaHost is under active development. We maintain a detailed task list with prioritized improvements:

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

### Getting Started with Development

1. Fork the repository
2. Set up your development environment following the prerequisites above
3. Review the [Project Organization Guide](docs/PROJECT_ORGANIZATION.md) to understand the codebase structure
4. Check the issues tab for tasks labeled "good first issue"

## Related Projects

- [LightHost](https://github.com/rolandoislas/LightHost): The original project that NovaHost is based on
- [JUCE Framework](https://juce.com/): The C++ framework used to build NovaHost

## Contributors

- [Rolando Islas](https://github.com/rolandoislas) - Original LightHost creator
- [blockie710](https://github.com/blockie710) - NovaHost fork maintainer

## License

This project is licensed under the GNU General Public License v2 - see the [license](license) file for details.

## Contact and Support

For questions, issues, or feature requests, please open an issue on the GitHub repository.