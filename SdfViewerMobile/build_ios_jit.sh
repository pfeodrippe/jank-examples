#!/bin/bash
# Build iOS app with JIT support for simulator
# This creates a separate build that includes LLVM for runtime compilation

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
JANK_SRC="/Users/pfeodrippe/dev/jank/compiler+runtime"
IOS_LLVM_DIR="$HOME/dev/ios-llvm-build/ios-llvm-simulator"

# JIT build directories (separate from AOT builds)
JANK_JIT_BUILD="$JANK_SRC/build-ios-sim"
JIT_BUILD_DIR="$PROJECT_DIR/build-iphonesimulator-jit"

echo "============================================"
echo "  iOS JIT Build for Simulator"
echo "============================================"
echo ""
echo "JANK JIT build: $JANK_JIT_BUILD"
echo "iOS LLVM: $IOS_LLVM_DIR"
echo "Output: $JIT_BUILD_DIR"
echo ""

# Check prerequisites
if [[ ! -f "$JANK_JIT_BUILD/libjank.a" ]]; then
    echo "ERROR: jank JIT library not found at $JANK_JIT_BUILD/libjank.a"
    echo "Run: cd $JANK_SRC && cmake -B build-ios-sim ... && make -C build-ios-sim"
    exit 1
fi

if [[ ! -d "$IOS_LLVM_DIR/lib" ]]; then
    echo "ERROR: iOS LLVM not found at $IOS_LLVM_DIR"
    echo "Run: cd $JANK_SRC && ./bin/build-ios-llvm simulator"
    exit 1
fi

# Create output directory
mkdir -p "$JIT_BUILD_DIR"

echo "Copying jank JIT libraries..."
cp "$JANK_JIT_BUILD/libjank.a" "$JIT_BUILD_DIR/"
cp "$JANK_JIT_BUILD/libjankzip.a" "$JIT_BUILD_DIR/"
cp "$JANK_JIT_BUILD/third-party/bdwgc/libgc.a" "$JIT_BUILD_DIR/"
cp "$JANK_JIT_BUILD/libfolly.a" "$JIT_BUILD_DIR/"

echo "Creating merged LLVM library..."
# Merge all LLVM/Clang libraries into one to simplify linking
cd "$JIT_BUILD_DIR"

# Get all LLVM and Clang static libraries
LLVM_LIBS=$(find "$IOS_LLVM_DIR/lib" -name "libLLVM*.a" -o -name "libclang*.a" -o -name "libCppInterOp*.a" 2>/dev/null | sort)

if [[ -z "$LLVM_LIBS" ]]; then
    echo "ERROR: No LLVM libraries found in $IOS_LLVM_DIR/lib"
    exit 1
fi

# Create a merged library using libtool (Apple's tool for this)
echo "Merging $(echo "$LLVM_LIBS" | wc -l | tr -d ' ') LLVM libraries..."
libtool -static -o libllvm_merged.a $LLVM_LIBS 2>/dev/null

echo ""
echo "Libraries ready in $JIT_BUILD_DIR:"
ls -lh "$JIT_BUILD_DIR"/*.a

echo ""
echo "============================================"
echo "  JIT Libraries Ready!"
echo "============================================"
echo ""
echo "Next steps:"
echo "1. Generate Xcode project: make ios-jit-project"
echo "2. Open in Xcode: open SdfViewerMobile/SdfViewerMobile-JIT.xcodeproj"
echo "3. Build and run from Xcode (required for JIT entitlements)"
