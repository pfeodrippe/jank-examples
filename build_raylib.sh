#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building Raylib (jank wrapper) ==="

RAYLIB_DIR="vendor/raylib"
DISTR_DIR="$RAYLIB_DIR/distr"
OBJS_DIR="$DISTR_DIR/objs"

# Check if base raylib.a exists
if [ ! -f "$DISTR_DIR/libraylib.a" ]; then
    echo "Error: $DISTR_DIR/libraylib.a not found"
    echo "Please ensure the raylib submodule is properly initialized with prebuilt libraries"
    exit 1
fi

# Extract object files from libraylib.a if not already done
if [ ! -d "$OBJS_DIR" ] || [ -z "$(ls -A $OBJS_DIR/*.o 2>/dev/null)" ]; then
    echo "Extracting object files from libraylib.a..."
    mkdir -p "$OBJS_DIR"
    cd "$OBJS_DIR"
    ar -x ../libraylib.a
    cd ../../..
    echo "Extracted object files"
fi

# Compile the jank wrapper
echo "Compiling raylib_jank_wrapper.cpp..."
clang++ -c -O2 -std=c++17 -fPIC \
    -I"$RAYLIB_DIR/src" \
    "$DISTR_DIR/raylib_jank_wrapper.cpp" \
    -o "$DISTR_DIR/raylib_jank_wrapper.o"

# Create static library by combining wrapper with raylib objects
echo "Creating libraylib_jank.a..."
ar rcs "$DISTR_DIR/libraylib_jank.a" \
    "$DISTR_DIR/raylib_jank_wrapper.o" \
    "$OBJS_DIR"/*.o

# Create dynamic library (macOS only for now)
if [ "$(uname)" = "Darwin" ]; then
    echo "Creating libraylib_jank.dylib..."
    clang++ -shared -o "$DISTR_DIR/libraylib_jank.dylib" \
        "$DISTR_DIR/raylib_jank_wrapper.o" \
        "$OBJS_DIR"/*.o \
        -framework Cocoa \
        -framework IOKit \
        -framework OpenGL \
        -framework CoreVideo \
        -framework CoreFoundation

    # Create symlink for jank --jit-lib
    ln -sf libraylib_jank.dylib "$DISTR_DIR/raylib_jank.dylib"
fi

echo ""
echo "=== Build complete ==="
echo "  - $DISTR_DIR/libraylib_jank.a (static library)"
if [ "$(uname)" = "Darwin" ]; then
    echo "  - $DISTR_DIR/libraylib_jank.dylib (dynamic library)"
fi
