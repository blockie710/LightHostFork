# NovaHost - Advanced Audio Plugin Host

A modern, feature-rich menubar/taskbar application for hosting VST/VST3/AU/AAX/ARA/LADSPA/LV2/AU v3 audio plugins on macOS, Windows, and Linux.

## About This Project

NovaHost (formerly LightHostFork) is a system-tray audio plugin host that provides a clean and efficient interface for loading, chaining, and managing multiple audio plugins. It's designed to be lightweight and unobtrusive while offering robust plugin hosting capabilities for audio producers, sound designers, and musicians.

### Key Features

- Multi-format plugin support (VST, VST3, AU, AAX, ARA, LADSPA, LV2, AU v3)
- Cross-platform compatibility (macOS, Windows, Linux)
- Convenient menubar/taskbar access
- Plugin chain management
- Plugin parameter control
- Low-latency audio processing
- MIDI support
- Streamlined workflow for audio production

## Development Roadmap

The project is currently focusing on:

### Active Development Areas
1. **Memory Management** - Replacing deprecated pointers, fixing leaks, implementing modern memory management patterns
2. **Thread Safety** - Resolving race conditions in UI and plugin scanning, improving multi-threading architecture
3. **Error Handling** - Implementing consistent error reporting and recovery systems

### Upcoming Improvements
- Code organization and style consistency
- C++ modernization (C++17/C++20 features)
- Enhanced plugin scanning and categorization
- Improved UI/UX across all platforms
- Comprehensive documentation and developer guides

## Get Involved

We welcome contributions! Check our [CONTRIBUTING.md](CONTRIBUTING.md) file for the complete task list and guidelines. Whether you're interested in coding, testing, documentation, or design, there are many ways to help improve NovaHost.

## Project Board

This GitHub Project board organizes tasks by priority:

1. 🔴 **High Priority** - Critical fixes for stability and performance
2. 🟠 **Medium Priority** - Code quality and organization improvements
3. 🟡 **Low Priority** - Feature enhancements and modernization

## Repository Structure

For details on the project's organization and architecture, see our [Project Organization Guide](docs/PROJECT_ORGANIZATION.md) and [Implementation Guide](docs/IMPLEMENTATION_GUIDE.md).

