#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building JoltPhysics for WASM ==="

# Common defines that MUST match between Jolt library and wrapper
# These affect JPH_VERSION_ID! If they don't match, RegisterTypes() will abort.
JOLT_DEFINES="-DJPH_DISABLE_CUSTOM_ALLOCATOR -DJPH_OBJECT_LAYER_BITS=32"

# Build Jolt with emscripten
# Force rebuild by checking a marker file, not just directory existence
REBUILD_MARKER="vendor/JoltPhysics/build-wasm/.build_complete"
if [ ! -f "$REBUILD_MARKER" ]; then
    echo "Building JoltPhysics with emscripten..."
    rm -rf vendor/JoltPhysics/build-wasm
    cd vendor/JoltPhysics
    mkdir -p build-wasm
    cd build-wasm

    # Configure with emscripten
    # Using flags matching JoltPhysics.js for compatibility
    emcmake cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DUSE_ASSERTS=OFF \
        -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF \
        -DPROFILER_IN_DEBUG_AND_RELEASE=OFF \
        -DINTERPROCEDURAL_OPTIMIZATION=OFF \
        -DFLOATING_POINT_EXCEPTIONS_ENABLED=OFF \
        -DOBJECT_LAYER_BITS=32 \
        -DDISABLE_CUSTOM_ALLOCATOR=ON \
        -DENABLE_OBJECT_STREAM=OFF \
        ..

    # Build
    emmake make -j8
    touch .build_complete
    cd ../../..
fi

# Check if libJolt.a exists
if [ ! -f "vendor/JoltPhysics/build-wasm/libJolt.a" ]; then
    echo "ERROR: libJolt.a not found in build-wasm"
    exit 1
fi

# Create distr directory for WASM
mkdir -p vendor/JoltPhysics/distr-wasm

# Extract object files from the WASM archive
echo "Extracting WASM object files..."
cd vendor/JoltPhysics/distr-wasm
rm -f *.o 2>/dev/null || true
emar -x ../build-wasm/libJolt.a
cd ../../..
echo "Extracted $(ls vendor/JoltPhysics/distr-wasm/*.o 2>/dev/null | wc -l) WASM object files"

# Compile jolt_wrapper.cpp for WASM
# CRITICAL: Use the same defines as the Jolt library build!
echo "Compiling jolt_wrapper.cpp for WASM..."
em++ -c -O2 -std=c++17 -DNDEBUG \
    $JOLT_DEFINES \
    -I vendor/JoltPhysics \
    vendor/jolt_wrapper.cpp \
    -o vendor/jolt_wrapper_wasm.o

# Combine all WASM object files into a single archive
echo "Creating combined WASM library..."
emar rcs vendor/jolt_combined_wasm.a \
    vendor/jolt_wrapper_wasm.o \
    vendor/JoltPhysics/distr-wasm/*.o

# Also create a single relocatable object file
echo "Creating single WASM object file..."
emcc -r -O2 \
    vendor/jolt_wrapper_wasm.o \
    vendor/JoltPhysics/distr-wasm/*.o \
    -o vendor/jolt_wasm.o

echo ""
echo "=== WASM Build complete ==="
echo ""
echo "Files created:"
echo "  - vendor/jolt_wrapper_wasm.o (wrapper only)"
echo "  - vendor/jolt_combined_wasm.a (archive with all objects)"
echo "  - vendor/jolt_wasm.o (single relocatable object)"
echo ""
echo "For emscripten-bundle, use:"
echo "  --lib vendor/jolt_wasm.o"
