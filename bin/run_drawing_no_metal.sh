#!/bin/bash
# Drawing Canvas - Run without Metal dylib to test
set -e
cd "$(dirname "$0")/.."

# Kill any existing drawing app
pkill -f "jank.*run-main vybe.app.drawing" 2>/dev/null || true
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
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="$JANK_DIR:/usr/bin:/bin:$PATH"
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export DYLD_LIBRARY_PATH=/opt/homebrew/lib

echo ""
echo "=========================================="
echo "   Drawing Canvas - NO METAL TEST"
echo "=========================================="
echo ""

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
    --framework MetalKit
    --framework QuartzCore
    --module-path src
    --lib /opt/homebrew/lib/libSDL3.dylib
    --lib /opt/homebrew/lib/libvulkan.dylib
)

echo "Running jank WITHOUT Metal dylib..."
jank "${JANK_ARGS[@]}" run-main vybe.app.drawing
