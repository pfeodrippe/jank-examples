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
elif [ -d "../SdfViewerMobile/Frameworks/SDL3.xcframework" ]; then
    # Reuse the dynamic SDL3 from SdfViewerMobile if available
    echo "Copying SDL3.xcframework from SdfViewerMobile..."
    cp -R ../SdfViewerMobile/Frameworks/SDL3.xcframework Frameworks/
    cp -r ../SdfViewerMobile/Frameworks/include/* Frameworks/include/ 2>/dev/null || true
    echo "SDL3.xcframework copied!"
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
