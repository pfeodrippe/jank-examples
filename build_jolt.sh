#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building JoltPhysics Wrapper ==="

# Check if Jolt is already built
if [ ! -d "vendor/JoltPhysics/build" ]; then
    echo "Building JoltPhysics..."
    cd vendor/JoltPhysics
    mkdir -p build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DUSE_ASSERTS=OFF \
          -DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF \
          -DPROFILER_IN_DEBUG_AND_RELEASE=OFF \
          -DINTERPROCEDURAL_OPTIMIZATION=OFF \
          ..
    make -j8
    cd ../../..
fi

# Extract object files if not already done
if [ ! -d "vendor/JoltPhysics/distr/objs" ] || [ -z "$(ls -A vendor/JoltPhysics/distr/objs 2>/dev/null)" ]; then
    echo "Extracting object files from libJolt.a..."
    mkdir -p vendor/JoltPhysics/distr/objs
    cd vendor/JoltPhysics/distr/objs
    ar -x ../../build/libJolt.a
    cd ../../../..
    echo "Extracted $(ls vendor/JoltPhysics/distr/objs/*.o | wc -l) object files"
fi

# Compile jolt_wrapper.cpp
echo "Compiling jolt_wrapper.cpp..."
clang++ -c -O2 -std=c++17 -DNDEBUG \
    -I vendor/JoltPhysics \
    vendor/jolt_wrapper.cpp \
    -o vendor/jolt_wrapper.o

echo "=== Build complete ==="
echo ""
echo "Object files ready:"
echo "  - vendor/jolt_wrapper.o (precompiled wrapper)"
echo "  - vendor/JoltPhysics/distr/objs/*.o (Jolt library)"
