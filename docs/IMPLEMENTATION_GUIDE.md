# NovaHost Project Implementation Guide

This document provides step-by-step instructions for implementing the project organization outlined in `PROJECT_ORGANIZATION.md`.

## Step 1: Directory Structure

Create the following directory structure (if not already present):

```
NovaHost/
├── build/                # Build artifacts and generated files
├── docs/                 # Documentation files
├── Installer/            # Installer scripts and resources
├── Resources/            # Application resources (icons, images)
├── Source/               # Main application source code
│   ├── Core/             # Core application functionality
│   ├── UI/               # User interface components
│   ├── Plugin/           # Plugin management code
│   └── Config/           # Configuration handling
├── Tests/                # Test files
├── third_party/          # Third-party libraries
└── Utilities/            # Utility scripts
```

## Step 2: File Organization

### Source Code Organization

1. Move source files to their appropriate subdirectories:
   - **Core/**: `HostStartup.cpp` (Application entry point)
   - **UI/**: `IconMenu.cpp`, `IconMenu.hpp` (Taskbar/menubar UI)
   - **Plugin/**: `PluginWindow.cpp`, `PluginWindow.h` (Plugin management)
   - **Config/**: Any configuration-related files (create placeholder files if needed)

2. Update include paths in all files to reflect the new directory structure.

### Documentation Consolidation

1. Move all documentation to the `docs/` directory:
   - Move `WINDOWS_INSTALL_GUIDE.md` files from both `Installer/` and `NovaHost/` to `docs/`
   - Ensure `PROJECT_ORGANIZATION.md` is in `docs/`
   - Update any references to documentation files in other files

## Step 3: Build System Updates

1. Update the Visual Studio project files:
   - Open `NovaHost.vcxproj` in a text editor
   - Update source file paths to reflect the new directory structure
   - Update include paths to include the new subdirectories

2. Update the JUCE project file:
   - Open `NovaHost.jucer` in Projucer
   - Add the new directory structure
   - Update source file paths and groups

## Step 4: Naming Standardization

1. Remove legacy files once transition is complete:
   - `LHFork.sln`
   - `LHFork.vcxproj`
   - `LHFork.vcxproj.filters`
   - `LHFork.vcxproj.user`
   - `LightHost.jucer`

2. Update references in remaining files:
   - Search for "LHFork" and "LightHostFork" and replace with "NovaHost"
   - Update project names in build configurations
   - Update installer scripts to use "NovaHost" naming

## Step 5: Installer Updates

1. Consolidate installer scripts:
   - Use `NovaHostSetup.iss` as the primary installer script
   - Update paths in installer scripts to reference the correct build artifacts

## Step 6: Testing

1. Build the project to verify that the organization changes haven't broken anything
2. Run existing tests to verify functionality
3. Update test references if needed to reflect new organization

## Step 7: README Updates

1. Ensure the `readme.md` file reflects:
   - The current project name (NovaHost)
   - The new directory structure
   - Updated build instructions
   - Correct paths to documentation

## Implementation Notes

- When moving files, prefer copying then verifying before deleting originals
- Make one organizational change at a time and verify the build still works
- Keep a backup of the original project structure in case rollback is needed
- Update .gitignore file to exclude the build artifacts in new locations