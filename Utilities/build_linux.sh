#!/bin/bash
# NovaHost Linux build script
# Created April 18, 2025

echo "NovaHost Linux Build Script"
echo "--------------------------"

# Check for required dependencies
echo "Checking for required dependencies..."
MISSING_DEPS=0

check_dependency() {
    if ! dpkg -l $1 &> /dev/null; then
        echo "❌ Missing dependency: $1"
        MISSING_DEPS=1
    fi
}

check_dependency libasound2-dev
check_dependency libx11-dev
check_dependency libxcomposite-dev
check_dependency libxcursor-dev
check_dependency libxinerama-dev
check_dependency libxrandr-dev
check_dependency libfreetype6-dev
check_dependency libcurl4-gnutls-dev

if [ $MISSING_DEPS -ne 0 ]; then
    echo "Please install missing dependencies with:"
    echo "sudo apt-get install libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxinerama-dev libxrandr-dev libfreetype6-dev libcurl4-gnutls-dev"
    exit 1
fi

echo "✅ All required dependencies are installed"

# Check for JUCE library
if [ ! -d "../lib/juce" ]; then
    echo "JUCE library not found in lib/juce"
    read -p "Would you like to clone JUCE now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Cloning JUCE library..."
        mkdir -p ../lib
        git clone https://github.com/juce-framework/JUCE.git ../lib/juce --depth=1
    else
        echo "JUCE library is required. Exiting."
        exit 1
    fi
fi

echo "✅ JUCE library is available"

# Check for build type
BUILD_TYPE="Release"
if [ "$1" == "debug" ]; then
    BUILD_TYPE="Debug"
    echo "Building in Debug mode"
else
    echo "Building in Release mode (use './build_linux.sh debug' for debug build)"
fi

# Create build directory
echo "Creating build directory..."
mkdir -p ../Builds/LinuxMakefile

# Check if Projucer is available for generating project
if [ -f "../lib/juce/Projucer" ]; then
    echo "Found Projucer, regenerating project files..."
    ../lib/juce/Projucer --resave ../NovaHost.jucer
fi

# Build the project
echo "Building NovaHost..."
cd ../Builds/LinuxMakefile
make CONFIG=$BUILD_TYPE

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "You can find the executable in: Builds/LinuxMakefile/build/"
else
    echo "❌ Build failed!"
    exit 1
fi

# Offer to run the application
read -p "Would you like to run NovaHost now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    cd build
    ./NovaHost
fi