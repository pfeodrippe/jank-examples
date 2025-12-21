#!/bin/bash
# Build jank code (vybe.sdf) for iOS via AOT compilation
# This cross-compiles the jank code to arm64 iOS native code

set -e
cd "$(dirname "$0")/.."

echo "============================================"
echo "  Building jank for iOS (AOT compilation)"
echo "============================================"
echo ""

# Determine jank paths
if [ -d "/Users/pfeodrippe/dev/jank/compiler+runtime" ]; then
    JANK_SRC="/Users/pfeodrippe/dev/jank/compiler+runtime"
elif [ -d "$HOME/jank/compiler+runtime" ]; then
    JANK_SRC="$HOME/jank/compiler+runtime"
else
    echo "Error: Could not find jank source directory"
    exit 1
fi

JANK_DIR="$JANK_SRC/build"
JANK_LIB_DIR="$JANK_DIR/llvm-install/usr/local/lib"

# iOS cross-compilation settings
IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
IOS_MIN_VERSION="15.0"
IOS_ARCH="arm64"
IOS_TRIPLE="arm64-apple-ios${IOS_MIN_VERSION}"

echo "iOS SDK: $IOS_SDK"
echo "Target: $IOS_TRIPLE"
echo ""

# Output directory for iOS build
IOS_BUILD_DIR="SdfViewerMobile/build"
mkdir -p "$IOS_BUILD_DIR"

# ============================================================================
# Step 1: Build jank runtime library for iOS
# ============================================================================
echo "Step 1: Building jank runtime for iOS..."

# The jank runtime needs to be compiled for iOS arm64
# This includes the core runtime, GC, and all jank standard library

JANK_RUNTIME_IOS="$IOS_BUILD_DIR/libjank_runtime_ios.a"

if [ ! -f "$JANK_RUNTIME_IOS" ]; then
    echo "  Note: jank iOS runtime not yet built"
    echo "  This requires cross-compiling the jank runtime for iOS"
    echo ""
    echo "  For now, we'll create a minimal native wrapper that calls"
    echo "  the pre-AOT-compiled jank modules."
    echo ""
fi

# ============================================================================
# Step 2: AOT compile vybe.sdf for iOS
# ============================================================================
echo "Step 2: AOT compiling vybe.sdf for iOS..."

# Object files needed (cross-compiled for iOS)
# These are the C/C++ dependencies that need to be built for iOS
IOS_OBJ_DIR="$IOS_BUILD_DIR/obj"
mkdir -p "$IOS_OBJ_DIR"

# Cross-compile ImGui for iOS
echo "  Cross-compiling ImGui for iOS..."
IMGUI_DIR="vendor/imgui"
IMGUI_CXXFLAGS="-std=c++17 -O2 -fPIC"
IMGUI_CXXFLAGS="$IMGUI_CXXFLAGS -target $IOS_TRIPLE -isysroot $IOS_SDK"
IMGUI_CXXFLAGS="$IMGUI_CXXFLAGS -I$IMGUI_DIR -I$IMGUI_DIR/backends"
IMGUI_CXXFLAGS="$IMGUI_CXXFLAGS -ISdfViewerMobile/Frameworks/SDL3.xcframework/ios-arm64/SDL3.framework/Headers"
IMGUI_CXXFLAGS="$IMGUI_CXXFLAGS -ISdfViewerMobile/Frameworks/MoltenVK.xcframework/ios-arm64/MoltenVK.framework/Headers"
IMGUI_CXXFLAGS="$IMGUI_CXXFLAGS -ISdfViewerMobile/Frameworks/include/vulkan"

# Compile ImGui core
for src in imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp; do
    if [ ! -f "$IOS_OBJ_DIR/${src%.cpp}.o" ]; then
        echo "    Compiling $src..."
        clang++ $IMGUI_CXXFLAGS -c "$IMGUI_DIR/$src" -o "$IOS_OBJ_DIR/${src%.cpp}.o"
    fi
done

# Compile ImGui backends
for src in imgui_impl_vulkan.cpp imgui_impl_sdl3.cpp; do
    if [ ! -f "$IOS_OBJ_DIR/${src%.cpp}.o" ]; then
        echo "    Compiling $src..."
        clang++ $IMGUI_CXXFLAGS -c "$IMGUI_DIR/backends/$src" -o "$IOS_OBJ_DIR/${src%.cpp}.o"
    fi
done

# Cross-compile Flecs for iOS
echo "  Cross-compiling Flecs for iOS..."
FLECS_CFLAGS="-O2 -fPIC -target $IOS_TRIPLE -isysroot $IOS_SDK"
if [ ! -f "$IOS_OBJ_DIR/flecs.o" ]; then
    clang $FLECS_CFLAGS -c vendor/flecs/distr/flecs.c -o "$IOS_OBJ_DIR/flecs.o"
fi

# Cross-compile STB implementation for iOS
echo "  Cross-compiling STB for iOS..."
if [ ! -f "$IOS_OBJ_DIR/stb_impl.o" ]; then
    clang $FLECS_CFLAGS -Ivulkan -c vulkan/stb_impl.c -o "$IOS_OBJ_DIR/stb_impl.o"
fi

# Cross-compile miniaudio for iOS
echo "  Cross-compiling miniaudio for iOS..."
if [ ! -f "$IOS_OBJ_DIR/miniaudio.o" ]; then
    clang $FLECS_CFLAGS -c vendor/vybe/miniaudio_impl.c -o "$IOS_OBJ_DIR/miniaudio.o"
fi

# ============================================================================
# Step 3: AOT compile jank modules
# ============================================================================
echo "Step 3: AOT compiling jank modules..."

# The jank compiler needs to be modified to support iOS cross-compilation
# For now, we output a placeholder message

echo ""
echo "============================================"
echo "  iOS jank AOT Compilation Status"
echo "============================================"
echo ""
echo "To run vybe.sdf on iOS with jank, the following is needed:"
echo ""
echo "1. Cross-compile jank runtime for iOS arm64"
echo "   - Build libcling/LLVM with iOS target support"
echo "   - Compile jank runtime libraries for iOS"
echo ""
echo "2. AOT compile vybe.sdf modules"
echo "   - jank compile --target arm64-apple-ios15.0 -o ios-modules vybe.sdf"
echo "   - This requires jank to support iOS as a target triple"
echo ""
echo "3. Link everything into the iOS app"
echo "   - iOS object files (ImGui, Flecs, etc.) - DONE"
echo "   - AOT-compiled jank modules"
echo "   - jank runtime library for iOS"
echo ""
echo "The iOS object files have been cross-compiled to:"
echo "  $IOS_OBJ_DIR/"
echo ""
ls -la "$IOS_OBJ_DIR/"
echo ""
echo "Next steps:"
echo "  1. Add iOS target support to jank compiler"
echo "  2. Cross-compile jank runtime for iOS"
echo "  3. AOT compile vybe.sdf with iOS target"
echo ""
