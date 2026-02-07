#!/bin/bash
# Simple build script for standalone applications

set -e

echo "Building standalone endoscope streaming applications..."
echo ""

# Check for dependencies
echo "Checking dependencies..."
if ! pkg-config --exists libusb-1.0; then
    echo "ERROR: libusb-1.0 not found. Please install it:"
    echo "  sudo apt-get install libusb-1.0-0-dev"
    exit 1
fi

if ! pkg-config --exists opencv4; then
    if ! pkg-config --exists opencv; then
        echo "ERROR: OpenCV not found. Please install it:"
        echo "  sudo apt-get install libopencv-dev"
        exit 1
    fi
fi

echo "All dependencies found!"
echo ""

# Create build directory
mkdir -p build
cd build

# Run cmake
echo "Running CMake..."
cmake -DCMAKE_BUILD_TYPE=Release ../CMakeLists_standalone.txt

# Build
echo "Building..."
make -j$(nproc)

echo ""
echo "Build complete! Executables are in the build/ directory:"
echo "  - endoscope_tcp_server"
echo "  - endoscope_udp_server"
echo "  - tcp_client"
echo ""
echo "To install system-wide, run:"
echo "  sudo make install"
