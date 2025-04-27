cmake_minimum_required(VERSION 3.15)
project(NovaHost VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(NOVAHOST_BUILD_TESTS "Build NovaHost tests" OFF)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find source files
file(GLOB_RECURSE SOURCES
    "Source/*.cpp"
    "Source/*.h"
    "Source/*.hpp"
)

# JUCE dependency (if needed)
# find_package(JUCE CONFIG REQUIRED)

# Add the executable
add_executable(NovaHost ${SOURCES})

# Set the output name to match your existing build
set_target_properties(NovaHost PROPERTIES
    OUTPUT_NAME "Nova Host"
)

# Include directories
target_include_directories(NovaHost PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/third_party
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(NovaHost PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    
    # Link with Windows libraries
    target_link_libraries(NovaHost PRIVATE
        winmm
    )
endif()

# Add tests if enabled
if(NOVAHOST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# Installation
install(TARGETS NovaHost
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install additional files
install(FILES
    "license"
    "gpl.txt"
    "readme.md"
    DESTINATION share/novahost
)

# Copy config files to build directory
configure_file(
    "${CMAKE_SOURCE_DIR}/Utilities/NovaHost_Debug.config"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/NovaHost_Debug.config"
    COPYONLY
)

# Message
message(STATUS "NovaHost configured with CMake ${CMAKE_VERSION}")