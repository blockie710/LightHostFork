# NovaHost Project Organization Plan

This document outlines the organization plan for the NovaHost project (formerly LightHostFork).

## Directory Structure

```
NovaHost/
├── build/               # Build artifacts and generated files
├── docs/                # Documentation files
├── Installer/           # Installer scripts and resources
├── Resources/           # Application resources (icons, images)
├── Source/              # Main application source code
├── Tests/               # Test files
├── third_party/         # Third-party libraries
├── Utilities/           # Utility scripts
└── LICENSE              # License file
```

## File Standardization

### Project Files
- Use `NovaHost.jucer` as the primary JUCE project file
- Use `NovaHost.sln` as the primary Visual Studio solution
- Remove legacy LHFork and LightHost files once transition is complete

### Naming Conventions
- Rename all instances of "LHFork" and "LightHostFork" to "NovaHost"
- Update all file headers and comments to reflect the new name
- Ensure consistent capitalization (NovaHost, not Novahost or NOVAHOST)

## Documentation
- Move all documentation files to the `docs/` directory
- Remove duplicate documentation files
- Update all documentation to reflect the current project name and features

## Build System
- Consolidate build scripts
- Use consistent build configurations across platforms
- Ensure build artifacts are directed to the `build/` directory

## Source Code Organization
1. Create properly organized subdirectories in Source:
   - `Core/` - Core application functionality
   - `UI/` - User interface components
   - `Plugin/` - Plugin management code
   - `Config/` - Configuration handling

## Installer
- Use a single installer configuration per platform
- Update all installer scripts to use the NovaHost branding
- Ensure installer scripts reference the correct build artifacts

## Implementation Timeline
1. Complete documentation consolidation
2. Standardize directory structure
3. Update project files
4. Organize source code
5. Update installer scripts
6. Final cleanup