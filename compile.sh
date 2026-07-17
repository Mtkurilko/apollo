#!/bin/bash

# Define the compiler and output executable name
COMPILER="g++"
OUTPUT_EXEC="/usr/local/bin/launchApollo"

# Define the directory where SFML libraries are typically installed on macOS
# This might need adjustment if you installed SFML elsewhere.
SFML_LIB_DIR="/usr/local/lib"

# Find all C++ source files
SOURCE_FILES=$(find . -name "*.cpp")

# Check if any source files were found
if [ -z "$SOURCE_FILES" ]; then
    echo "No C++ source files found."
    exit 1
fi

# Compile and link all files
# -std=c++17: Enables C++17 features, necessary for SFML.
# -o "$OUTPUT_EXEC": Sets the output executable name.
# ${SOURCE_FILES}: Includes all found C++ source files.
# -L"${SFML_LIB_DIR}": Tells the compiler to look for libraries in this directory.
# -Wl,-rpath,"${SFML_LIB_DIR}": Embeds the library search path into the executable for runtime.
# -lsfml-graphics -lsfml-window -lsfml-system: Links the core SFML libraries.
"${COMPILER}" -std=c++17 -o "${OUTPUT_EXEC}" ${SOURCE_FILES} -L"${SFML_LIB_DIR}" -Wl,-rpath,"${SFML_LIB_DIR}" -lsfml-graphics -lsfml-window -lsfml-system

# Check the exit status of the g++ command
if [ $? -eq 0 ]; then
    echo "Compilation successful. Executable: ${OUTPUT_EXEC}"
else
    echo "Compilation failed."
    exit 1
fi
