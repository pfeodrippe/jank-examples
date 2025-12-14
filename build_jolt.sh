#!/bin/bash
set -e

cd "$(dirname "$0")"

# Use system clang (avoid inheriting jank's custom CC/CXX)
export CC=/usr/bin/clang
export CXX=/usr/bin/clang++

echo "=== Building JoltPhysics Wrapper ==="

# Create root CMakeLists.txt if it doesn't exist (needed for simple library build)
if [ ! -f "vendor/JoltPhysics/CMakeLists.txt" ]; then
    echo "Creating JoltPhysics CMakeLists.txt..."
    cat > vendor/JoltPhysics/CMakeLists.txt << 'CMAKEOF'
cmake_minimum_required(VERSION 3.16)
project(JoltPhysics)

# Disable asserts and debug features
set(USE_ASSERTS OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)

set(PHYSICS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Include Jolt cmake file - creates Jolt target
include(${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake)
CMAKEOF
fi

# Check if Jolt is already built (check for actual library, not just directory)
if [ ! -f "vendor/JoltPhysics/build/libJolt.a" ]; then
    echo "Building JoltPhysics..."
    rm -rf vendor/JoltPhysics/build  # Clean any partial build
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
