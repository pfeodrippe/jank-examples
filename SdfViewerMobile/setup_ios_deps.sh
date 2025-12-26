#!/bin/bash
# Setup iOS dependencies for SDF Viewer Mobile
# Downloads and builds SDL3 and MoltenVK frameworks for iOS

set -e
cd "$(dirname "$0")"

FRAMEWORKS_DIR="Frameworks"
TEMP_DIR="temp_build"

echo "============================================"
echo "  SDF Viewer Mobile - iOS Dependency Setup"
echo "============================================"
echo ""

mkdir -p "$FRAMEWORKS_DIR"
mkdir -p "$TEMP_DIR"

# ============================================================================
# MoltenVK - Vulkan to Metal translation
# ============================================================================
setup_moltenvk() {
    echo "Setting up MoltenVK..."

    if [ -d "$FRAMEWORKS_DIR/MoltenVK.xcframework" ]; then
        echo "  MoltenVK already exists, skipping..."
        return
    fi

    # Download MoltenVK release
    MOLTENVK_VERSION="1.2.9"
    MOLTENVK_URL="https://github.com/KhronosGroup/MoltenVK/releases/download/v${MOLTENVK_VERSION}/MoltenVK-all.tar"

    echo "  Downloading MoltenVK v${MOLTENVK_VERSION}..."
    curl -L "$MOLTENVK_URL" -o "$TEMP_DIR/MoltenVK.tar"

    echo "  Extracting..."
    tar -xf "$TEMP_DIR/MoltenVK.tar" -C "$TEMP_DIR"

    # Copy iOS framework (static version for iOS)
    if [ -d "$TEMP_DIR/MoltenVK/MoltenVK/static/MoltenVK.xcframework" ]; then
        cp -R "$TEMP_DIR/MoltenVK/MoltenVK/static/MoltenVK.xcframework" "$FRAMEWORKS_DIR/"
        echo "  MoltenVK.xcframework installed (static)"
    else
        echo "  ERROR: MoltenVK.xcframework not found in release"
        ls -la "$TEMP_DIR/MoltenVK/" 2>/dev/null || true
        return 1
    fi

    # Also need vulkan headers (including vk_video for video codec support)
    if [ -d "$TEMP_DIR/MoltenVK/MoltenVK/include" ]; then
        mkdir -p "$FRAMEWORKS_DIR/include"
        cp -R "$TEMP_DIR/MoltenVK/MoltenVK/include/vulkan" "$FRAMEWORKS_DIR/include/"
        cp -R "$TEMP_DIR/MoltenVK/MoltenVK/include/MoltenVK" "$FRAMEWORKS_DIR/include/"
        # vk_video contains Vulkan video codec headers (H.264, H.265, AV1, etc.)
        if [ -d "$TEMP_DIR/MoltenVK/MoltenVK/include/vk_video" ]; then
            cp -R "$TEMP_DIR/MoltenVK/MoltenVK/include/vk_video" "$FRAMEWORKS_DIR/include/"
            echo "  Vulkan headers installed (including vk_video)"
        else
            echo "  Vulkan headers installed (vk_video not found in release)"
        fi
    fi
}

# ============================================================================
# SDL3 - Cross-platform windowing
# ============================================================================
setup_sdl3() {
    echo "Setting up SDL3..."

    if [ -d "$FRAMEWORKS_DIR/SDL3.xcframework" ]; then
        echo "  SDL3 already exists, skipping..."
        return
    fi

    # SDL3 needs to be built from source for iOS
    # We'll clone and build using CMake + Xcode
    SDL3_VERSION="release-3.2.0"
    SDL3_REPO="https://github.com/libsdl-org/SDL.git"

    if [ ! -d "$TEMP_DIR/SDL" ]; then
        echo "  Cloning SDL3..."
        git clone --depth 1 --branch "$SDL3_VERSION" "$SDL3_REPO" "$TEMP_DIR/SDL"
    fi

    echo "  Building SDL3 for iOS..."
    cd "$TEMP_DIR/SDL"

    # Build for iOS device (arm64)
    mkdir -p build-ios
    cd build-ios
    cmake .. \
        -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_VULKAN=ON \
        -DSDL_METAL=ON \
        -DSDL_TEST=OFF

    cmake --build . --config Release -- -sdk iphoneos CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO

    cd ../../..

    # Build for iOS Simulator (x86_64 + arm64)
    echo "  Building SDL3 for iOS Simulator..."
    cd "$TEMP_DIR/SDL"
    mkdir -p build-ios-sim
    cd build-ios-sim
    cmake .. \
        -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
        -DCMAKE_OSX_SYSROOT=iphonesimulator \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_VULKAN=ON \
        -DSDL_METAL=ON \
        -DSDL_TEST=OFF

    cmake --build . --config Release -- -sdk iphonesimulator CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO

    cd ../../..

    # Create xcframework from dylibs
    echo "  Creating SDL3.xcframework from dylibs..."

    # Find the built dylibs (they're named libSDL3.0.dylib)
    IOS_DYLIB="$TEMP_DIR/SDL/build-ios/Release-iphoneos/libSDL3.0.dylib"
    SIM_DYLIB="$TEMP_DIR/SDL/build-ios-sim/Release-iphonesimulator/libSDL3.0.dylib"

    if [ ! -f "$IOS_DYLIB" ] || [ ! -f "$SIM_DYLIB" ]; then
        echo "  ERROR: Could not find built SDL3 dylibs"
        echo "  iOS dylib: $IOS_DYLIB (exists: $([ -f "$IOS_DYLIB" ] && echo yes || echo no))"
        echo "  Simulator dylib: $SIM_DYLIB (exists: $([ -f "$SIM_DYLIB" ] && echo yes || echo no))"
        return 1
    fi

    echo "  iOS dylib: $IOS_DYLIB"
    echo "  Simulator dylib: $SIM_DYLIB"

    # Copy headers
    mkdir -p "$FRAMEWORKS_DIR/include/SDL3"
    cp -R "$TEMP_DIR/SDL/include/SDL3/"* "$FRAMEWORKS_DIR/include/SDL3/"

    # Create xcframework from libraries
    xcodebuild -create-xcframework \
        -library "$IOS_DYLIB" -headers "$TEMP_DIR/SDL/include" \
        -library "$SIM_DYLIB" -headers "$TEMP_DIR/SDL/include" \
        -output "$FRAMEWORKS_DIR/SDL3.xcframework"

    echo "  SDL3.xcframework created"
}

# ============================================================================
# shaderc - Runtime shader compilation (optional)
# ============================================================================
setup_shaderc() {
    echo "Setting up shaderc..."

    # shaderc is optional on iOS since we pre-compile shaders
    # But we include it for potential runtime shader hot-reloading

    if [ -d "$FRAMEWORKS_DIR/libshaderc" ]; then
        echo "  shaderc already exists, skipping..."
        return
    fi

    echo "  Note: shaderc for iOS requires manual building"
    echo "  For now, shaders must be pre-compiled to SPIR-V on the host"
    echo "  Use 'make build-shaders' before building the iOS app"
}

# ============================================================================
# Compile Shaders
# ============================================================================
compile_shaders() {
    echo "Compiling shaders to SPIR-V..."

    SHADER_DIR="../vulkan_kim"

    if ! command -v glslangValidator &> /dev/null; then
        echo "  WARNING: glslangValidator not found"
        echo "  Install with: brew install glslang"
        echo "  Shaders won't be compiled"
        return
    fi

    for shader in "$SHADER_DIR"/*.comp; do
        if [ -f "$shader" ]; then
            output="${shader%.comp}.spv"
            echo "  Compiling $(basename "$shader")..."
            glslangValidator -V "$shader" -o "$output"
        fi
    done

    for shader in "$SHADER_DIR"/*.vert; do
        if [ -f "$shader" ]; then
            output="${shader}.spv"
            echo "  Compiling $(basename "$shader")..."
            glslangValidator -V "$shader" -o "$output"
        fi
    done

    for shader in "$SHADER_DIR"/*.frag; do
        if [ -f "$shader" ]; then
            output="${shader}.spv"
            echo "  Compiling $(basename "$shader")..."
            glslangValidator -V "$shader" -o "$output"
        fi
    done

    echo "  Shaders compiled"
}

# ============================================================================
# Main
# ============================================================================

echo "Step 1: MoltenVK"
setup_moltenvk

echo ""
echo "Step 2: SDL3"
setup_sdl3

echo ""
echo "Step 3: shaderc (optional)"
setup_shaderc

echo ""
echo "Step 4: Compile Shaders"
compile_shaders

echo ""
echo "============================================"
echo "  Cleanup"
echo "============================================"
rm -rf "$TEMP_DIR"
echo "Temporary files removed"

echo ""
echo "============================================"
echo "  Setup Complete!"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Generate Xcode project: xcodegen generate"
echo "  2. Open project: open SdfViewerMobile.xcodeproj"
echo "  3. Build and run on device"
echo ""
