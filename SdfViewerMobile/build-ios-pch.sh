#!/bin/bash
# Build iOS pre-compiled header for jank JIT
# This script must be run on macOS with the jank compiler+runtime build available
#
# Usage: ./build-ios-pch.sh [target]
#   target: 'simulator' (default) or 'device'

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Use env var or default for local dev
JANK_SRC="${JANK_SRC:-/Users/pfeodrippe/dev/jank/compiler+runtime}"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/jank-resources}"

# Parse target argument (default: simulator)
TARGET="${1:-simulator}"
if [[ "$TARGET" != "simulator" && "$TARGET" != "device" ]]; then
    echo "Error: Invalid target '$TARGET'. Must be 'simulator' or 'device'"
    exit 1
fi

# Set SDK and target triple based on target
if [[ "$TARGET" == "simulator" ]]; then
    IOS_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)
    TARGET_TRIPLE="arm64-apple-ios17.0-simulator"
else
    IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
    TARGET_TRIPLE="arm64-apple-ios17.0"
fi

echo "Building PCH for iOS $TARGET"
echo "Using iOS SDK: $IOS_SDK"
echo "Target triple: $TARGET_TRIPLE"

# Use the jank LLVM clang (same version as iOS LLVM)
LLVM_CLANG="$JANK_SRC/build/llvm-install/usr/local/bin/clang++"
if [ ! -f "$LLVM_CLANG" ]; then
    echo "Error: clang++ not found at $LLVM_CLANG"
    echo "Please build jank first with: ./bin/configure && ./bin/compile"
    exit 1
fi

echo "Using clang: $LLVM_CLANG"
$LLVM_CLANG --version | head -1

# Always sync jank headers to output directory to ensure consistency
# The PCH must use the same header paths as cross-compilation
INCLUDE_DIR="$OUTPUT_DIR/include"
echo "Syncing jank headers to $INCLUDE_DIR..."
mkdir -p "$INCLUDE_DIR"
rm -rf "$INCLUDE_DIR/jank" "$INCLUDE_DIR/jtl"
cp -r "$JANK_SRC/include/cpp/"* "$INCLUDE_DIR/"
echo "Headers synced."

# Create iOS PCH wrapper that includes prelude + fstream (needed by native code)
PCH_WRAPPER="$OUTPUT_DIR/ios_pch_wrapper.hpp"
cat > "$PCH_WRAPPER" << 'WRAPPER_EOF'
// iOS PCH wrapper - includes prelude plus additional standard headers
// that user native code may need but aren't in the jank prelude
#pragma once

#include <jank/prelude.hpp>

// Standard library headers needed by native user code
#include <fstream>
WRAPPER_EOF
echo "Created PCH wrapper: $PCH_WRAPPER"

# Prelude header location - use the wrapper for PCH build
PRELUDE_HEADER="$PCH_WRAPPER"
if [ ! -f "$INCLUDE_DIR/jank/prelude.hpp" ]; then
    echo "Error: prelude.hpp not found at $INCLUDE_DIR/jank/prelude.hpp"
    echo "Make sure headers were copied correctly."
    exit 1
fi

# Output PCH path - use different names for simulator vs device
if [[ "$TARGET" == "simulator" ]]; then
    OUTPUT_PCH="$OUTPUT_DIR/incremental.pch"
else
    OUTPUT_PCH="$OUTPUT_DIR/incremental-device.pch"
fi
mkdir -p "$OUTPUT_DIR"

echo "Building PCH for iOS $TARGET (arm64)..."
echo "  Input: $PRELUDE_HEADER"
echo "  Output: $OUTPUT_PCH"

# Build the PCH with iOS target and same flags as runtime
# IMPORTANT: Use -nostdinc++ and explicitly add iOS SDK's libc++ headers
# This ensures the PCH uses iOS SDK headers, not LLVM's bundled libc++
$LLVM_CLANG \
    -target "$TARGET_TRIPLE" \
    -isysroot "$IOS_SDK" \
    -nostdinc++ \
    -isystem "$IOS_SDK/usr/include/c++/v1" \
    -std=gnu++20 \
    -DIMMER_HAS_LIBGC=1 -DIMMER_TAGGED_NODE=0 -DHAVE_CXX14=1 \
    -DFOLLY_HAVE_JEMALLOC=0 -DFOLLY_HAVE_TCMALLOC=0 \
    -DFOLLY_ASSUME_NO_JEMALLOC=1 -DFOLLY_ASSUME_NO_TCMALLOC=1 \
    -DJANK_TARGET_IOS=1 \
    -frtti \
    -I"$INCLUDE_DIR" \
    -I"$JANK_SRC/src/cpp" \
    -I"$JANK_SRC/third-party/immer" \
    -I"$JANK_SRC/third-party/bdwgc/include" \
    -I"$JANK_SRC/third-party/bpptree/include" \
    -I"$JANK_SRC/third-party/boost-preprocessor/include" \
    -I"$JANK_SRC/third-party/boost-multiprecision/include" \
    -I"$JANK_SRC/third-party/folly" \
    -I"$JANK_SRC/third-party/stduuid/include" \
    -Xclang -fincremental-extensions \
    -Xclang -emit-pch \
    -Xclang -fmodules-embed-all-files \
    -fno-modules-validate-system-headers \
    -fpch-instantiate-templates \
    -Xclang -fno-validate-pch \
    -Xclang -fno-pch-timestamp \
    -x c++-header \
    -o "$OUTPUT_PCH" \
    -c "$PRELUDE_HEADER"

if [ -f "$OUTPUT_PCH" ]; then
    PCH_SIZE=$(ls -lh "$OUTPUT_PCH" | awk '{print $5}')
    echo "Success! PCH built: $OUTPUT_PCH ($PCH_SIZE)"
else
    echo "Error: PCH was not created"
    exit 1
fi
