#!/bin/bash
# iOS AOT build script using jank's ios-bundle
# Handles: module discovery, C++ generation, cross-compilation, library bundling
#
# Usage: ./build_ios_jank_aot.sh <target>
#   target: 'simulator' or 'device'

set -e
cd "$(dirname "$0")/.."

# Require explicit target argument
if [[ "$1" != "simulator" && "$1" != "device" ]]; then
    echo "Error: You must specify a target: 'simulator' or 'device'"
    echo "Usage: $0 <target>"
    exit 1
fi

TARGET="$1"

# Determine jank path
JANK_SRC="${JANK_SRC:-/Users/pfeodrippe/dev/jank/compiler+runtime}"
if [[ ! -d "$JANK_SRC" ]]; then
    echo "Error: jank source directory not found: $JANK_SRC"
    exit 1
fi

# Output directory (uses PLATFORM_NAME-compatible names)
if [[ "$TARGET" == "simulator" ]]; then
    OUTPUT_DIR="SdfViewerMobile/build-iphonesimulator"
else
    OUTPUT_DIR="SdfViewerMobile/build-iphoneos"
fi

# Build iOS AOT bundle using jank's ios-bundle
# Note: build-sdf-deps should be run before this script (handled by Makefile)
"$JANK_SRC/bin/ios-bundle" \
  --entry-module vybe.sdf.ios \
  --module-path src \
  --output-dir "$OUTPUT_DIR" \
  --output-library libvybe_aot.a \
  -I . \
  -I vendor/imgui \
  -I vendor/imgui/backends \
  -I vendor/flecs/distr \
  -I vendor/miniaudio \
  -I vendor \
  -I /opt/homebrew/include \
  -I /opt/homebrew/include/SDL3 \
  -I SdfViewerMobile/Frameworks/include \
  -L /opt/homebrew/lib \
  --jit-lib /opt/homebrew/lib/libvulkan.dylib \
  --jit-lib /opt/homebrew/lib/libSDL3.dylib \
  --jit-lib /opt/homebrew/lib/libshaderc_shared.dylib \
  --jit-lib "$PWD/vulkan/libsdf_deps.dylib" \
  "$TARGET"
