#!/bin/bash
# Build jank code (vybe.sdf) for iOS via AOT compilation
# Uses the WASM AOT approach: generate C++ on macOS, cross-compile for iOS
#
# Usage: ./build_ios_jank_aot.sh <target>
#   target: 'simulator' or 'device'

set -e
cd "$(dirname "$0")/.."

# Require explicit target argument
if [[ "$1" != "simulator" && "$1" != "device" ]]; then
    echo "Error: You must specify a target: 'simulator' or 'device'"
    echo ""
    echo "Usage: $0 <target>"
    echo "  simulator  - Build for iOS Simulator (arm64-apple-ios-simulator)"
    echo "  device     - Build for iOS Device (arm64-apple-ios)"
    echo ""
    exit 1
fi

# Set IOS_SIMULATOR based on argument
if [[ "$1" == "simulator" ]]; then
    IOS_SIMULATOR=true
else
    IOS_SIMULATOR=false
fi

echo "============================================"
echo "  Building jank for iOS (C++ AOT approach)"
echo "  Target: $1"
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

# Entry point module - all other modules are auto-detected from dependencies
ENTRY_MODULE="vybe.sdf.ios"

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

# Auto-detect modules from entry point using --list-modules
# This gets all modules in dependency order, filtering out clojure.* (pre-compiled in jank iOS build)
echo "  Discovering modules from $ENTRY_MODULE..."
ALL_MODULES=$("$JANK_BIN" "${JANK_FLAGS[@]}" compile-module --list-modules "$ENTRY_MODULE" 2>&1 | grep -v "^\[jank\]" | grep -v "^WARNING:")

# Filter to only include user modules (exclude clojure.* which are pre-compiled)
VYBE_MODULES=()
while IFS= read -r module; do
    if [[ -n "$module" && ! "$module" =~ ^clojure\. ]]; then
        VYBE_MODULES+=("$module")
    fi
done <<< "$ALL_MODULES"

echo "  Modules to compile (in dependency order):"
for module in "${VYBE_MODULES[@]}"; do
    echo "    - $module"
done
echo ""

echo "  Compiling modules..."
echo ""

for module in "${VYBE_MODULES[@]}"; do
    # Convert module name to filename (. to _ for filename, / for path)
    module_filename=$(echo "$module" | tr '.' '_')
    GENERATED_CPP="$IOS_GENERATED_DIR/${module_filename}_generated.cpp"

    # Convert module name to source path (e.g., vybe.sdf.ios -> src/vybe/sdf/ios.jank)
    source_path="src/$(echo "$module" | tr '.' '/').jank"

    # Check if we need to recompile (source newer than generated, or generated doesn't exist)
    if [ -f "$GENERATED_CPP" ] && [ -f "$source_path" ] && [ "$GENERATED_CPP" -nt "$source_path" ]; then
        echo "  Skipping $module (up-to-date)"
        continue
    fi

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
# Step 1.5: Generate jank_aot_init.cpp
# ============================================================================
echo ""
echo "Step 1.5: Generating jank_aot_init.cpp..."

INIT_FILE="$IOS_GENERATED_DIR/jank_aot_init.cpp"

cat > "$INIT_FILE" << 'HEADER'
// Auto-generated by build_ios_jank_aot.sh
// DO NOT EDIT - This file is regenerated on each build

#include <iostream>

// Core library load functions
extern "C" void* jank_load_clojure_core_native();
extern "C" void* jank_load_core();
extern "C" void* jank_load_string();
extern "C" void* jank_load_set();
extern "C" void* jank_load_walk();
extern "C" void* jank_load_template__();  // double underscore because 'template' is C++ reserved word
extern "C" void* jank_load_test();

HEADER

# Add extern declarations for application modules
for module in "${VYBE_MODULES[@]}"; do
    func_name="jank_load_$(echo "$module" | tr '.' '_')"
    echo "extern \"C\" void* ${func_name}();" >> "$INIT_FILE"
done

cat >> "$INIT_FILE" << 'MIDDLE'

// Single entry point - loads all modules in correct order
extern "C" void jank_aot_init() {
    std::cout << "[jank] Loading core libraries..." << std::endl;

    // Core libraries (always needed)
    jank_load_clojure_core_native();
    jank_load_core();
    jank_load_string();
    jank_load_set();
    jank_load_walk();
    jank_load_template__();  // double underscore because 'template' is C++ reserved word
    jank_load_test();

    std::cout << "[jank] Loading application modules..." << std::endl;

MIDDLE

# Add load calls for application modules
for module in "${VYBE_MODULES[@]}"; do
    func_name="jank_load_$(echo "$module" | tr '.' '_')"
    echo "    std::cout << \"[jank] Loading $module...\" << std::endl;" >> "$INIT_FILE"
    echo "    ${func_name}();" >> "$INIT_FILE"
done

cat >> "$INIT_FILE" << 'FOOTER'

    std::cout << "[jank] All modules loaded successfully!" << std::endl;
}
FOOTER

echo "  Generated: $INIT_FILE"

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

    # Check if we need to recompile (cpp newer than obj, or obj doesn't exist)
    if [ -f "$GENERATED_OBJ" ] && [ "$GENERATED_OBJ" -nt "$GENERATED_CPP" ]; then
        echo "  Skipping $module_filename (up-to-date)"
        continue
    fi

    echo "  Compiling $module_filename..."
    clang++ $IOS_CXXFLAGS -c "$GENERATED_CPP" -o "$GENERATED_OBJ" 2>&1

    if [ -f "$GENERATED_OBJ" ]; then
        echo "    -> $(ls -lh "$GENERATED_OBJ" | awk '{print $5}')"
    else
        echo "    ERROR: Failed to compile $module"
        exit 1
    fi
done

# Compile jank_aot_init.cpp
INIT_CPP="$IOS_GENERATED_DIR/jank_aot_init.cpp"
INIT_OBJ="$IOS_OBJ_DIR/jank_aot_init.o"

# Always recompile jank_aot_init.cpp (it's small and may change when modules change)
echo "  Compiling jank_aot_init.cpp..."
clang++ $IOS_CXXFLAGS -c "$INIT_CPP" -o "$INIT_OBJ" 2>&1

if [ -f "$INIT_OBJ" ]; then
    echo "    -> $(ls -lh "$INIT_OBJ" | awk '{print $5}')"
else
    echo "    ERROR: Failed to compile jank_aot_init.cpp"
    exit 1
fi

echo ""
echo "  All modules compiled successfully."

# ============================================================================
# Step 3: Copy jank libraries and create vybe_aot static library
# ============================================================================
echo ""
echo "Step 3: Copying jank libraries and creating vybe_aot static library..."
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

# Create a single static library from ALL .o files (core + user modules + init)
# This makes project.yml fully dynamic - just link -lvybe_aot
echo "  Creating libvybe_aot.a from all object files..."
ar rcs "$IOS_BUILD_DIR/libvybe_aot.a" "$IOS_OBJ_DIR"/*.o
echo "    -> $(ls -lh "$IOS_BUILD_DIR/libvybe_aot.a" | awk '{print $5}')"

echo ""
echo "  Libraries in $IOS_BUILD_DIR/:"
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
