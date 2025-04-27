# NovaHost Project Organization Plan

This document outlines the organization plan for the NovaHost project (formerly LightHostFork).

## Directory Structure

```
NovaHost/
├── build/                # Build artifacts and generated files
│   ├── Debug/            # Debug build outputs
│   └── Release/          # Release build outputs
├── docs/                 # Documentation files
│   ├── API/              # API documentation
│   ├── User/             # User guides
│   └── Dev/              # Developer documentation
├── Installer/            # Installer scripts and resources
│   ├── Output/           # Generated installers
│   ├── Resources/        # Installer-specific resources
│   └── Scripts/          # Helper scripts for installation
├── Resources/            # Application resources
│   ├── Icons/            # Application icons
│   └── Images/           # Other image resources
├── Source/               # Main application source code
│   ├── Core/             # Core application functionality
│   │   ├── Audio/        # Audio engine code
│   │   └── Plugin/       # Plugin hosting infrastructure
│   ├── UI/               # User interface components
│   │   ├── MenuBar/      # Menu bar interface
│   │   └── Windows/      # Application windows
│   ├── Config/           # Configuration handling code
│   └── Util/             # Utility functions
├── Tests/                # Test files
│   ├── Unit/             # Unit tests
│   └── Integration/      # Integration tests
├── third_party/          # Third-party libraries
│   └── JUCE/             # JUCE framework
├── Utilities/            # Utility scripts
└── LICENSE               # License file
```

## File Standardization

### Project Files
- Use `NovaHost.jucer` as the primary JUCE project file
  - Update module paths and project settings for cross-platform compatibility
  - Configure appropriate export targets for all supported platforms
- Use `NovaHost.sln` as the primary Visual Studio solution
  - Include all required projects and dependencies
  - Configure proper build configurations (Debug, Release, Testing)
- Remove legacy LHFork and LightHost files once transition is complete
  - Archive or document any important historical information
  - Update all references to old project files in documentation

### Naming Conventions
- Rename all instances of "LHFork" and "LightHostFork" to "NovaHost"
  - Update file names, class names, namespace names
  - Update build artifact names and paths
  - Update installer scripts and resources
- Update all file headers and comments to reflect the new name
  - Use a standard header format with copyright notice
  - Include file description and licensing information
- Ensure consistent capitalization (NovaHost, not Novahost or NOVAHOST)
  - Use PascalCase for class names
  - Use camelCase for variable and method names
  - Use snake_case for file names

## Documentation
- Move all documentation files to the `docs/` directory
  - Organize by audience (User, Developer, API)
  - Create consistent documentation format using Markdown
- Remove duplicate documentation files
  - Consolidate information where needed
  - Cross-reference between documents instead of duplicating content
- Update all documentation to reflect the current project name and features
  - Update screenshots to show the current UI
  - Update command-line options and parameters
  - Update building instructions for all platforms
- Add comprehensive API documentation
  - Document public interfaces and classes
  - Include usage examples and best practices
- Create detailed development guides
  - Environment setup instructions
  - Contribution workflow
  - Testing procedures

## Version Control
- Create a proper branching strategy
  - `main` - stable releases
  - `dev` - development branch
  - `feature/*` - feature branches
  - `fix/*` - bugfix branches
- Add descriptive GitHub project metadata
  - Update issue templates
  - Configure pull request templates
  - Set up GitHub Actions for CI/CD
- Apply appropriate Git attributes and ignores
  - Use `.gitattributes` for line ending management
  - Ensure binary files are marked appropriately
  - Ignore build artifacts and IDE-specific files

## Build System
- Consolidate build scripts
  - Create unified build scripts for all platforms
  - Use platform-specific scripts only when necessary
- Use consistent build configurations across platforms
  - Debug configuration with debugging symbols and assertions
  - Release configuration with optimizations
  - Testing configuration for running automated tests
- Ensure build artifacts are directed to the `build/` directory
  - Organize by configuration (Debug/Release)
  - Include version information in artifact names
- Set up continuous integration
  - Automated builds on commit/pull request
  - Run tests as part of the build process
  - Package installers for distribution

## Source Code Organization
1. Create properly organized subdirectories in Source:
   - `Core/` - Core application functionality
     - Application entry points
     - Audio processing infrastructure
     - Plugin host implementation
   - `UI/` - User interface components
     - System tray/menu bar integration
     - Plugin windows and dialogs
     - Settings UI
   - `Plugin/` - Plugin management code
     - Plugin scanning and loading
     - Plugin parameter handling
     - Format-specific adapters (VST, VST3, AU, etc.)
   - `Config/` - Configuration handling
     - Settings serialization
     - User preferences management
     - Plugin chain persistence
   - `Util/` - Utility functions and classes
     - String handling
     - File operations
     - Error reporting

## Testing Framework
- Implement comprehensive unit testing
  - Use JUCE's built-in testing framework
  - Cover core functionality with unit tests
  - Add mocks for external dependencies
- Add integration tests
  - Test plugin loading and parameter handling
  - Test audio processing pipeline
  - Test UI interactions
- Create testing documentation
  - How to run tests
  - How to add new tests
  - How to interpret test results

## Installer
- Use a single installer configuration per platform
  - Windows: Inno Setup
  - macOS: DMG or PKG
  - Linux: DEB and RPM packages
- Update all installer scripts to use the NovaHost branding
  - Update icons, images, and text
  - Update registry keys and file associations
  - Update installation paths
- Ensure installer scripts reference the correct build artifacts
  - Use relative paths where possible
  - Include all required dependencies
  - Create proper uninstallation procedures

## Implementation Timeline
1. Complete documentation consolidation (Week 1)
   - Update GitHub-related files
   - Consolidate duplicate documentation
   - Update naming and references
2. Standardize directory structure (Week 2)
   - Set up folder structure as defined above
   - Move files to appropriate locations
   - Update reference paths
3. Update project files (Week 3)
   - Update JUCE project files
   - Update IDE-specific files
   - Configure build settings
4. Organize source code (Week 4-5)
   - Refactor code into appropriate directories
   - Update include paths and references
   - Fix any broken functionality
5. Update installer scripts (Week 6)
   - Update Windows installer
   - Create/update macOS installer
   - Create/update Linux packages
6. Final cleanup (Week 7)
   - Remove legacy files and references
   - Update documentation with new structure
   - Create release notes for the reorganization

## Contribution Management
- Create detailed contribution guidelines
  - Code style guide
  - Pull request process
  - Issue reporting template
- Establish code review process
  - Required reviewers for different components
  - Code quality standards
  - Testing requirements
- Set up automation for common tasks
  - Code formatting checks
  - Static analysis
  - Dependency management