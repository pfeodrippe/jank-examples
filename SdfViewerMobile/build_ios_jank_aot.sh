#!/bin/bash
# Build jank code (vybe.sdf) for iOS via AOT compilation
# Uses the WASM AOT approach: generate C++ on macOS, cross-compile for iOS

set -e
cd "$(dirname "$0")/.."

echo "============================================"
echo "  Building jank for iOS (C++ AOT approach)"
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
JANK_BIN="$JANK_DIR/jank"

# Verify jank binary exists
if [ ! -x "$JANK_BIN" ]; then
    echo "Error: jank binary not found at $JANK_BIN"
    echo "Please build jank first: cd $JANK_SRC && ./bin/compile"
    exit 1
fi

# Check if building for simulator
IOS_SIMULATOR="${IOS_SIMULATOR:-false}"

# iOS cross-compilation settings (iOS 17.0+ required for std::format)
if [[ "${IOS_SIMULATOR}" == "true" ]]; then
  IOS_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)
  IOS_MIN_VERSION="17.0"
  IOS_ARCH="arm64"
  IOS_TRIPLE="arm64-apple-ios${IOS_MIN_VERSION}-simulator"
  JANK_IOS_DIR="$JANK_SRC/build-ios-simulator"
  # xcframework simulator subdirectory
  XCFRAMEWORK_SUBDIR="ios-arm64_x86_64-simulator"
else
  IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
  IOS_MIN_VERSION="17.0"
  IOS_ARCH="arm64"
  IOS_TRIPLE="arm64-apple-ios${IOS_MIN_VERSION}"
  JANK_IOS_DIR="$JANK_SRC/build-ios"
  # xcframework device subdirectory
  XCFRAMEWORK_SUBDIR="ios-arm64"
fi

echo "jank: $JANK_BIN"
echo "iOS SDK: $IOS_SDK"
echo "Target: $IOS_TRIPLE"
echo ""

# Output directory for iOS build (separate dirs for simulator vs device)
# Uses PLATFORM_NAME-compatible names so Xcode can reference them with $(PLATFORM_NAME)
if [[ "${IOS_SIMULATOR}" == "true" ]]; then
  IOS_BUILD_DIR="SdfViewerMobile/build-iphonesimulator"
else
  IOS_BUILD_DIR="SdfViewerMobile/build-iphoneos"
fi
IOS_GENERATED_DIR="SdfViewerMobile/generated"
mkdir -p "$IOS_BUILD_DIR"
mkdir -p "$IOS_GENERATED_DIR"

# Set up environment for jank
export SDKROOT=$(xcrun --show-sdk-path)
export CC="$JANK_DIR/llvm-install/usr/local/bin/clang"
export CXX="$JANK_DIR/llvm-install/usr/local/bin/clang++"

# ============================================================================
# Step 0: Build jank runtime for iOS
# ============================================================================
echo "Step 0: Building jank runtime for iOS..."
echo ""

"$JANK_SRC/bin/ios-bundle" --skip-build 2>&1 | tail -20

# Check if iOS libraries exist
if [ ! -f "$JANK_IOS_DIR/libjank.a" ]; then
    echo ""
    echo "Building jank iOS runtime..."
    "$JANK_SRC/bin/build-ios" "$JANK_IOS_DIR" Release
fi

echo ""
echo "  jank iOS libraries:"
echo "    - $JANK_IOS_DIR/libjank.a"
echo "    - $JANK_IOS_DIR/libjankzip.a"
echo "    - $JANK_IOS_DIR/third-party/bdwgc/libgc.a"
echo ""

# ============================================================================
# Step 1: Generate C++ from jank using WASM AOT codegen
# ============================================================================
echo "Step 1: Generating C++ from vybe.sdf using WASM AOT codegen..."
echo ""

# First, ensure the desktop build works (to have all deps ready)
echo "  Ensuring dependencies are built..."
make build-sdf-deps JANK_SRC="$JANK_SRC" 2>&1 | tail -5

# Common jank flags for all module compilations
JANK_FLAGS=(
    --module-path src
    --codegen wasm-aot
    --save-cpp
    -I .
    -I vendor/imgui
    -I vendor/imgui/backends
    -I vendor/flecs/distr
    -I vendor/miniaudio
    -I vendor
    -I /opt/homebrew/include
    -I /opt/homebrew/include/SDL3
    -L /opt/homebrew/lib
    --jit-lib /opt/homebrew/lib/libvulkan.dylib
    --jit-lib /opt/homebrew/lib/libSDL3.dylib
    --jit-lib /opt/homebrew/lib/libshaderc_shared.dylib
    --jit-lib "$PWD/vulkan/libsdf_deps.dylib"
)

# Modules to compile (in dependency order)
# Each module must be compiled so its jank_load_* function is available at runtime
VYBE_MODULES=(
    "vybe.util"
    "vybe.sdf.math"
    "vybe.sdf.state"
    "vybe.sdf.shader"
    "vybe.sdf.ui"
    "vybe.sdf.ios"
)

echo "  Compiling vybe.sdf modules..."
echo ""

for module in "${VYBE_MODULES[@]}"; do
    # Convert module name to filename (. to _ for filename, / for path)
    module_filename=$(echo "$module" | tr '.' '_')
    GENERATED_CPP="$IOS_GENERATED_DIR/${module_filename}_generated.cpp"

    echo "  Compiling $module..."
    "$JANK_BIN" "${JANK_FLAGS[@]}" \
        --save-cpp-path "$GENERATED_CPP" \
        compile-module "$module" 2>&1 | tail -5

    if [ -f "$GENERATED_CPP" ]; then
        echo "    -> $(wc -l < "$GENERATED_CPP") lines"
    else
        echo "    ERROR: Failed to generate C++ for $module"
        exit 1
    fi
done

echo ""
echo "  Generated C++ files:"
ls -lh "$IOS_GENERATED_DIR"/*.cpp

# Add missing includes to generated files (until jank compiler is rebuilt with these)
echo ""
echo "  Adding missing includes to generated files..."
for cppfile in "$IOS_GENERATED_DIR"/*.cpp; do
    if ! grep -q "opaque_box.hpp" "$cppfile"; then
        sed -i '' 's|#include <jank/runtime/core/meta.hpp>|#include <jank/runtime/core/meta.hpp>\
#include <jank/runtime/obj/opaque_box.hpp>\
#include <jank/c_api.h>|' "$cppfile"
    fi
done

# ============================================================================
# Step 2: Cross-compile generated C++ for iOS
# ============================================================================
echo ""
echo "Step 2: Cross-compiling generated C++ for iOS..."
echo ""

IOS_OBJ_DIR="$IOS_BUILD_DIR/obj"
mkdir -p "$IOS_OBJ_DIR"

# iOS compiler flags
# Note: -Wno-c++11-narrowing needed because jank codegen produces narrowing in some places
IOS_CXXFLAGS="-std=c++20 -O2 -fPIC -Wno-c++11-narrowing"
IOS_CXXFLAGS="$IOS_CXXFLAGS -target $IOS_TRIPLE -isysroot $IOS_SDK"

# jank include paths
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/include/cpp"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/immer"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/folly"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/bpptree/include"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/cli11/include"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/boost-preprocessor/include"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/boost-multiprecision/include"
IOS_CXXFLAGS="$IOS_CXXFLAGS -I$JANK_SRC/third-party/bdwgc/include"

# Project include paths (for native headers referenced in generated code)
IOS_CXXFLAGS="$IOS_CXXFLAGS -I."
IOS_CXXFLAGS="$IOS_CXXFLAGS -Ivendor/imgui"
IOS_CXXFLAGS="$IOS_CXXFLAGS -Ivendor/imgui/backends"
IOS_CXXFLAGS="$IOS_CXXFLAGS -Ivendor/flecs/distr"
IOS_CXXFLAGS="$IOS_CXXFLAGS -Ivendor/miniaudio"
IOS_CXXFLAGS="$IOS_CXXFLAGS -Ivendor"

# iOS framework include paths (use XCFRAMEWORK_SUBDIR for simulator vs device)
IOS_CXXFLAGS="$IOS_CXXFLAGS -ISdfViewerMobile/Frameworks/SDL3.xcframework/$XCFRAMEWORK_SUBDIR/SDL3.framework/Headers"
IOS_CXXFLAGS="$IOS_CXXFLAGS -ISdfViewerMobile/Frameworks/MoltenVK.xcframework/$XCFRAMEWORK_SUBDIR/MoltenVK.framework/Headers"
IOS_CXXFLAGS="$IOS_CXXFLAGS -ISdfViewerMobile/Frameworks/include"

# Define macros for iOS (same as WASM to reuse code paths)
IOS_CXXFLAGS="$IOS_CXXFLAGS -DJANK_TARGET_IOS=1"
IOS_CXXFLAGS="$IOS_CXXFLAGS -DJANK_TARGET_WASM=1"
IOS_CXXFLAGS="$IOS_CXXFLAGS -DJANK_TARGET_EMSCRIPTEN=1"
IOS_CXXFLAGS="$IOS_CXXFLAGS -DIMMER_HAS_LIBGC=1"
IOS_CXXFLAGS="$IOS_CXXFLAGS -DIMMER_TAGGED_NODE=0"
IOS_CXXFLAGS="$IOS_CXXFLAGS -DSDF_AOT_BUILD=1"

echo "  Compiling generated C++ for iOS..."
echo ""

# Compile all generated vybe modules
for module in "${VYBE_MODULES[@]}"; do
    module_filename=$(echo "$module" | tr '.' '_')
    GENERATED_CPP="$IOS_GENERATED_DIR/${module_filename}_generated.cpp"
    GENERATED_OBJ="$IOS_OBJ_DIR/${module_filename}_generated.o"

    echo "  Compiling $module_filename..."
    clang++ $IOS_CXXFLAGS -c "$GENERATED_CPP" -o "$GENERATED_OBJ" 2>&1

    if [ -f "$GENERATED_OBJ" ]; then
        echo "    -> $(ls -lh "$GENERATED_OBJ" | awk '{print $5}')"
    else
        echo "    ERROR: Failed to compile $module"
        exit 1
    fi
done

if [ -f "$IOS_OBJ_DIR/vybe_sdf_ios_generated.o" ]; then
    echo "  Compiled to: $IOS_OBJ_DIR/vybe_sdf_ios_generated.o"
    echo "  Size: $(ls -lh "$IOS_OBJ_DIR/vybe_sdf_ios_generated.o" | awk '{print $5}')"
else
    echo "ERROR: Cross-compilation failed"
    exit 1
fi

# ============================================================================
# Step 3: Copy jank libraries to iOS build directory
# ============================================================================
echo ""
echo "Step 3: Copying jank libraries to iOS build directory..."
echo ""

cp "$JANK_IOS_DIR/libjank.a" "$IOS_BUILD_DIR/"
cp "$JANK_IOS_DIR/libjankzip.a" "$IOS_BUILD_DIR/"
cp "$JANK_IOS_DIR/third-party/bdwgc/libgc.a" "$IOS_BUILD_DIR/"

# Copy core library object files
cp "$JANK_IOS_DIR/clojure_core_generated.o" "$IOS_OBJ_DIR/" 2>/dev/null || true
cp "$JANK_IOS_DIR/clojure_set_generated.o" "$IOS_OBJ_DIR/" 2>/dev/null || true
cp "$JANK_IOS_DIR/clojure_string_generated.o" "$IOS_OBJ_DIR/" 2>/dev/null || true
cp "$JANK_IOS_DIR/clojure_walk_generated.o" "$IOS_OBJ_DIR/" 2>/dev/null || true
cp "$JANK_IOS_DIR/clojure_template_generated.o" "$IOS_OBJ_DIR/" 2>/dev/null || true
cp "$JANK_IOS_DIR/clojure_test_generated.o" "$IOS_OBJ_DIR/" 2>/dev/null || true

echo "  Copied libraries to: $IOS_BUILD_DIR/"
ls -lh "$IOS_BUILD_DIR"/*.a

# ============================================================================
# Step 4: Summary
# ============================================================================
echo ""
echo "============================================"
echo "  iOS AOT Build Complete!"
echo "============================================"
echo ""
echo "Generated files:"
echo "  - $GENERATED_CPP (C++ source)"
echo "  - $IOS_OBJ_DIR/vybe_sdf_generated.o (iOS arm64 object)"
echo ""
echo "jank libraries:"
echo "  - $IOS_BUILD_DIR/libjank.a"
echo "  - $IOS_BUILD_DIR/libjankzip.a"
echo "  - $IOS_BUILD_DIR/libgc.a"
echo ""
echo "Core library objects:"
echo "  - $IOS_OBJ_DIR/clojure_core_generated.o"
echo "  - $IOS_OBJ_DIR/clojure_set_generated.o"
echo "  - $IOS_OBJ_DIR/clojure_string_generated.o"
echo "  - $IOS_OBJ_DIR/clojure_walk_generated.o"
echo "  - $IOS_OBJ_DIR/clojure_template_generated.o"
echo "  - $IOS_OBJ_DIR/clojure_test_generated.o"
echo ""
echo "Next steps:"
echo "  1. Add libraries and objects to Xcode project"
echo "  2. Call jank_init() and jank_load_* functions from iOS code"
echo ""
