#!/bin/bash
# Setup iOS dependencies for DrawingMobile
# Downloads SDL3 xcframework (no Vulkan/MoltenVK needed for 2D drawing)

set -e
cd "$(dirname "$0")"

echo "Setting up iOS dependencies for DrawingMobile..."
echo ""

# Create Frameworks directory
mkdir -p Frameworks/include

# ============================================================================
# SDL3 (cross-platform windowing + 2D rendering with Metal backend)
# Use dynamic library to avoid linking all SDL dependencies manually
# ============================================================================
if [ -d "Frameworks/SDL3.xcframework" ]; then
    echo "SDL3.xcframework already exists, skipping..."
else
    echo "Building SDL3 for iOS (dynamic library)..."

    # Clone SDL3
    if [ ! -d "sdl3-src" ]; then
        git clone --depth 1 https://github.com/libsdl-org/SDL.git sdl3-src
    fi

    # Build for iOS Device (arm64) - DYNAMIC library
    echo "Building SDL3 for iOS device..."
    cmake -S sdl3-src -B sdl3-build-ios \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF
    cmake --build sdl3-build-ios --config Release

    # Build for iOS Simulator (arm64 + x86_64) - DYNAMIC library
    echo "Building SDL3 for iOS simulator..."
    cmake -S sdl3-src -B sdl3-build-sim \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT=iphonesimulator \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF
    cmake --build sdl3-build-sim --config Release

    # Create xcframework using dylibs
    echo "Creating SDL3.xcframework..."
    xcodebuild -create-xcframework \
        -library sdl3-build-ios/libSDL3.dylib -headers sdl3-src/include \
        -library sdl3-build-sim/libSDL3.dylib -headers sdl3-src/include \
        -output Frameworks/SDL3.xcframework

    # Copy headers for include path
    cp -r sdl3-src/include/* Frameworks/include/
    mkdir -p Frameworks/include/SDL3
    cp -r sdl3-src/include/SDL3/* Frameworks/include/SDL3/ 2>/dev/null || true

    # Cleanup
    rm -rf sdl3-src sdl3-build-ios sdl3-build-sim

    echo "SDL3.xcframework created!"
fi

# ============================================================================
# iOS SDK C headers (needed for device JIT compilation)
# Copies stdlib.h, stdio.h, etc. from iOS SDK for JIT include paths
# ============================================================================
if [ -d "jank-resources/include/sys_include" ]; then
    echo "sys_include already exists, skipping..."
else
    IOS_SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
    if [ -d "$IOS_SDK_PATH/usr/include" ]; then
        echo "Copying iOS SDK C headers to sys_include..."
        cp -r "$IOS_SDK_PATH/usr/include" jank-resources/include/sys_include
        echo "sys_include copied! ($(ls jank-resources/include/sys_include | wc -l | tr -d ' ') files)"
    else
        echo "WARNING: iOS SDK not found at $IOS_SDK_PATH"
        echo "Device JIT will fail without iOS SDK C headers."
    fi
fi

echo ""
echo "============================================"
echo "iOS dependencies setup complete!"
echo "============================================"
echo ""
echo "Frameworks:"
echo "  - SDL3.xcframework (Metal-based 2D rendering)"
echo ""
echo "Next steps:"
echo "  make drawing-ios-jit-sim-run  # Build and run in simulator"
