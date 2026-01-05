#!/bin/bash
# Drawing Canvas - Run the drawing app
set -e
cd "$(dirname "$0")/.."

# Kill any existing drawing app (but not other jank apps!)
pkill -f "jank.*run-main vybe.app.drawing" 2>/dev/null || true
# Also kill by port 5580 (drawing app nREPL port)
lsof -ti:5580 2>/dev/null | xargs kill -9 2>/dev/null || true
sleep 0.5

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

# Platform-specific environment setup
case "$(uname -s)" in
    Darwin)
        export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
        export PATH="$JANK_DIR:/usr/bin:/bin:$PATH"

        # Vulkan environment for MoltenVK
        export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
        export DYLD_LIBRARY_PATH=/opt/homebrew/lib
        ;;
    Linux)
        if [ -d "$HOME/jank/compiler+runtime/build" ]; then
            export PATH="$HOME/jank/compiler+runtime/build:$PATH"
            JANK_DIR="$HOME/jank/compiler+runtime/build"
        fi
        ;;
esac

echo ""
echo "=========================================="
echo "   Drawing Canvas - Phase 1"
echo "=========================================="
echo ""

# Build jank arguments for drawing app
case "$(uname -s)" in
    Darwin)
        JANK_ARGS=(
            -I/opt/homebrew/include
            -I/opt/homebrew/include/SDL3
            -I.
            -Isrc
            -L/opt/homebrew/lib
            --framework Cocoa
            --framework IOKit
            --framework IOSurface
            --framework Metal
            --framework QuartzCore
            --module-path src
            --lib /opt/homebrew/lib/libSDL3.dylib
            --lib /opt/homebrew/lib/libvulkan.dylib
        )
        ;;
    Linux)
        JANK_ARGS=(
            -I.
            -Isrc
            --module-path src
            --lib libSDL3.so
            --lib libvulkan.so
        )
        ;;
esac

echo "Running jank..."
jank "${JANK_ARGS[@]}" run-main vybe.app.drawing
