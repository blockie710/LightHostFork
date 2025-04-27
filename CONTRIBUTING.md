# Contributing to NovaHost

Thank you for your interest in contributing to NovaHost (formerly LightHostFork)! This document outlines the current development roadmap, prioritized tasks, and guidelines for contributing to the project.

## Development Roadmap

The following tasks are organized by priority to help focus development efforts on the most critical improvements.

### 🔴 High Priority Tasks

#### Memory Management

- [ ] Replace all `ScopedPointer` usages with `std::unique_ptr` or JUCE's `UniquePtr`
- [ ] Review and fix potential memory leaks in plugin instance handling
- [ ] Audit resource cleanup in `PluginWindow` class destructors
- [ ] Implement RAII patterns consistently throughout the codebase
- [ ] Add smart pointer usage guidelines to documentation

#### Thread Safety

- [ ] Ensure UI operations run only on the message thread (use `MessageManager::callAsync`)
- [ ] Add thread safety for plugin scanning process
- [ ] Fix race condition in plugin timeout detection
- [ ] Add thread identification logging in debug mode
- [ ] Implement thread-safe access to shared resources

#### Error Handling

- [ ] Implement consistent error reporting mechanism
- [ ] Add user-facing error notifications for plugin failures
- [ ] Create error logging system for debugging issues
- [ ] Handle exceptions consistently across the codebase
- [ ] Add error recovery strategies for plugin crashes

### 🟠 Medium Priority Tasks

#### Code Organization

- [ ] Remove duplicated code in `PluginWindow.cpp`
- [ ] Extract repeated declarations of `ProcessorProgramPropertyComp` class
- [ ] Consolidate redundant method implementations (`refresh()`, `audioProcessorChanged()`, etc.)
- [ ] Create proper utility classes for commonly used functions
- [ ] Move XML configuration data out of source code files
- [ ] Organize source files according to PROJECT_ORGANIZATION.md structure

#### Coding Style Consistency

- [ ] Standardize on tabs vs spaces throughout the codebase
- [ ] Implement consistent bracing style
- [ ] Standardize naming conventions for variables, methods, and classes
- [ ] Add comprehensive code documentation
- [ ] Create style guide document for future contributors

### 🟡 Lower Priority Tasks

#### C++ Modernization

- [ ] Leverage more C++17 features (structured bindings, std::optional, etc.)
- [ ] Update to newer JUCE APIs and patterns
- [ ] Replace legacy code patterns with modern equivalents
- [ ] Implement JUCE best practices throughout
- [ ] Evaluate C++20 features for possible adoption

#### User Experience Improvements

- [ ] Enhance plugin scanning progress reporting
- [ ] Improve multi-instance handling logic
- [ ] Add additional plugin categorization options
- [ ] Create more detailed audio device configuration options
- [ ] Implement customizable UI themes

#### Documentation

- [ ] Create comprehensive API documentation
- [ ] Add developer setup guide with environment configuration steps
- [ ] Document plugin compatibility considerations
- [ ] Create troubleshooting guide for common issues
- [ ] Create user manual with screenshots and usage examples

## Contributing Guidelines

### Getting Started

1. Fork the repository
2. Clone your fork locally: `git clone https://github.com/YOUR_USERNAME/NovaHost.git`
3. Set up the development environment following the instructions in [README.md](readme.md)
4. Create a new branch for your feature or fix: `git checkout -b feature/your-feature-name`

### Development Environment Setup

1. Install the required dependencies:
   - JUCE Framework (included in repository)
   - Platform-specific development tools:
     - Windows: Visual Studio 2019 or newer
     - macOS: Xcode 12 or newer
     - Linux: GCC 9+ or Clang 10+, CodeBlocks or Make
2. Open the appropriate project file for your platform
3. Build the project to verify your development environment

### Pull Request Process

1. Update documentation to reflect any changes you've made
2. Ensure your code follows the existing style conventions
3. Run all tests and ensure they pass
4. Submit a pull request with a clear description of the changes
5. Respond to code review feedback and make necessary adjustments

### Code Style Guidelines

- Follow the existing code style of the project
- Write clear, descriptive comments for complex logic
- Use meaningful variable and function names
- Keep functions focused on a single responsibility
- Add unit tests for new functionality
- Document public interfaces

## License

By contributing to this project, you agree that your contributions will be licensed under the project's [GPL v2 license](license).

## Communication

- For questions about contributing, please open an issue on the GitHub repository
- For feature discussions, use the Discussions tab on GitHub
- For reporting bugs, use the Issues tab with the bug template

## Acknowledgments

We appreciate all contributors who help improve NovaHost! All contributors will be acknowledged in the README.md file.
