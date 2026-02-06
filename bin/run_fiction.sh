#!/bin/bash
# Fiction Viewer - Runs the narrative game
set -e
cd "$(dirname "$0")/.."

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

# Platform-specific environment setup
case "$(uname -s)" in
    Darwin)
        export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
        export PATH="$JANK_DIR:/usr/bin:/bin:$PATH"
        export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
        export DYLD_LIBRARY_PATH=/opt/homebrew/lib
        ;;
    Linux)
        if [ -d "$HOME/jank/compiler+runtime/build" ]; then
            export PATH="$HOME/jank/compiler+runtime/build:$PATH"
            JANK_DIR="$HOME/jank/compiler+runtime/build"
            JANK_LIB_DIR="$JANK_DIR/llvm-install/usr/local/lib"
        fi
        ;;
esac

# Build fiction shaders
echo "Building fiction shaders..."
make build-fiction-shaders JANK_SRC="$JANK_SRC"

# Build dependencies (flecs, etc.)
echo "Building dependencies..."
make build-sdf-deps JANK_SRC="$JANK_SRC"

# Object files needed for JIT
OBJ_FILES=(
    vendor/flecs/distr/flecs.o
    vendor/vybe/vybe_flecs_jank.o
    vulkan/stb_impl.o
)

# Build jank arguments
case "$(uname -s)" in
    Darwin)
        JANK_ARGS=(
            -I/opt/homebrew/include
            -I/opt/homebrew/include/SDL3
            -I.
            -Ivendor
            -Ivendor/flecs/distr
            -L/opt/homebrew/lib
            --framework Cocoa
            --framework IOKit
            --framework IOSurface
            --framework Metal
            --framework QuartzCore
            --module-path src
        )
        DYLIBS=(
            /opt/homebrew/lib/libvulkan.dylib
            /opt/homebrew/lib/libSDL3.dylib
        )
        ;;
    Linux)
        JANK_ARGS=(
            -I/usr/include
            -I/usr/local/include
            -I/usr/local/include/SDL3
            -I.
            -Ivendor
            -Ivendor/flecs/distr
            -L/usr/lib
            -L/usr/lib/x86_64-linux-gnu
            -L/usr/lib/aarch64-linux-gnu
            -L/usr/local/lib
            --module-path src
        )
        DYLIBS=()
        for lib in libvulkan.so libSDL3.so; do
            lib_path=$(ldconfig -p 2>/dev/null | grep "$lib" | head -1 | awk '{print $NF}')
            if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
                DYLIBS+=("$lib_path")
            fi
        done
        ;;
esac

# Add object files and libraries
for obj in "${OBJ_FILES[@]}"; do
    JANK_ARGS+=(--obj "$obj")
done
for lib in "${DYLIBS[@]}"; do
    JANK_ARGS+=(--lib "$lib")
done

echo ""
echo "============================================"
echo "  LA VOITURE - Interactive Fiction"
echo "============================================"
echo ""

# Run the fiction game
jank "${JANK_ARGS[@]}" run-main fiction
