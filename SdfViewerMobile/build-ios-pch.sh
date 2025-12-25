#!/bin/bash
# Build iOS pre-compiled header for jank JIT
# This script must be run on macOS with the jank compiler+runtime build available

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Use env var or default for local dev
JANK_SRC="${JANK_SRC:-/Users/pfeodrippe/dev/jank/compiler+runtime}"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/jank-resources}"

# Get iOS simulator SDK path
IOS_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)
echo "Using iOS SDK: $IOS_SDK"

# Use the jank LLVM clang (same version as iOS LLVM)
LLVM_CLANG="$JANK_SRC/build/llvm-install/usr/local/bin/clang++"
if [ ! -f "$LLVM_CLANG" ]; then
    echo "Error: clang++ not found at $LLVM_CLANG"
    echo "Please build jank first with: ./bin/configure && ./bin/compile"
    exit 1
fi

echo "Using clang: $LLVM_CLANG"
$LLVM_CLANG --version | head -1

# Prelude header location
PRELUDE_HEADER="$JANK_SRC/include/cpp/jank/prelude.hpp"
if [ ! -f "$PRELUDE_HEADER" ]; then
    echo "Error: prelude.hpp not found at $PRELUDE_HEADER"
    exit 1
fi

# Output PCH path
OUTPUT_PCH="$OUTPUT_DIR/incremental.pch"
mkdir -p "$OUTPUT_DIR"

echo "Building PCH for iOS simulator (arm64)..."
echo "  Input: $PRELUDE_HEADER"
echo "  Output: $OUTPUT_PCH"

# Build the PCH with iOS target and same flags as runtime
# IMPORTANT: Use -nostdinc++ and explicitly add iOS SDK's libc++ headers
# This ensures the PCH uses iOS SDK headers, not LLVM's bundled libc++
$LLVM_CLANG \
    -target arm64-apple-ios17.0-simulator \
    -isysroot "$IOS_SDK" \
    -nostdinc++ \
    -isystem "$IOS_SDK/usr/include/c++/v1" \
    -std=gnu++20 \
    -DIMMER_HAS_LIBGC=1 -DIMMER_TAGGED_NODE=0 -DHAVE_CXX14=1 \
    -DFOLLY_HAVE_JEMALLOC=0 -DFOLLY_HAVE_TCMALLOC=0 \
    -DFOLLY_ASSUME_NO_JEMALLOC=1 -DFOLLY_ASSUME_NO_TCMALLOC=1 \
    -DJANK_TARGET_IOS=1 \
    -frtti \
    -I"$JANK_SRC/include/cpp" \
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
