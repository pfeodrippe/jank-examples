#!/bin/bash
# Fiction WASM Build - Builds the Fiction narrative game for WebGPU browser
set -e

cd "$(dirname "$0")/.."

JANK_DIR="/Users/pfeodrippe/dev/jank/compiler+runtime"
SOMETHING_DIR="/Users/pfeodrippe/dev/something"

echo "=== Building Fiction WASM (WebGPU) ==="
echo ""

# Build Flecs for WASM if needed
if [ ! -f "vendor/flecs/distr/flecs_wasm.o" ]; then
    echo "Building Flecs for WASM..."
    cd vendor/flecs/distr && emcc -c -fPIC -o flecs_wasm.o flecs.c
    cd "$SOMETHING_DIR"
fi

# Build vybe flecs helpers for WASM if needed
if [ ! -f "vendor/vybe/vybe_flecs_jank_wasm.o" ]; then
    echo "Building vybe_flecs_jank for WASM..."
    make build-vybe-wasm
fi

# Build fiction_gfx stub for native JIT symbol resolution
echo "Building fiction_gfx native stub..."
mkdir -p fiction_gfx/build
"$JANK_DIR/build/llvm-install/usr/local/bin/clang++" \
    -std=c++20 -O2 -c -fPIC \
    --sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    -Ifiction_gfx -Ivendor -Ivendor/flecs/distr \
    fiction_gfx/fiction_gfx_stub.cpp \
    -o fiction_gfx/build/fiction_gfx_stub.o

# Build fiction_gfx native dylib for JIT
echo "Building fiction_gfx native dylib..."
"$JANK_DIR/build/llvm-install/usr/local/bin/clang++" \
    -std=c++20 -O2 -dynamiclib -Wl,-undefined,dynamic_lookup \
    --sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
    -Ifiction_gfx -Ivendor -Ivendor/flecs/distr \
    fiction_gfx/fiction_gfx_stub.cpp \
    -o fiction_gfx/libfiction_gfx_stub.dylib

# Build fiction_gfx WASM object
echo "Building fiction_gfx WASM object..."
mkdir -p fiction_gfx/build-wasm
em++ -std=c++20 -O2 -c -fPIC \
    --use-port=emdawnwebgpu \
    -DFICTION_USE_WEBGPU -DJANK_TARGET_EMSCRIPTEN -DJANK_TARGET_WASM=1 \
    -DSTBI_NO_THREAD_LOCALS \
    -I"$JANK_DIR/include/cpp" \
    -I"$JANK_DIR/third-party" \
    -I"$JANK_DIR/third-party/bdwgc/include" \
    -I"$JANK_DIR/third-party/immer" \
    -I"$JANK_DIR/third-party/folly" \
    -I"$JANK_DIR/third-party/boost-multiprecision/include" \
    -I"$JANK_DIR/third-party/boost-preprocessor/include" \
    -I"$JANK_DIR/third-party/stduuid/include" \
    -Ifiction_gfx -Ivulkan -Ivendor -Ivendor/flecs/distr \
    fiction_gfx/fiction_gfx_webgpu.cpp \
    -o fiction_gfx/build-wasm/fiction_gfx_wasm.o 2>&1 || {
        # If fiction_gfx_webgpu.cpp doesn't exist, create a minimal one
        echo "Creating fiction_gfx_webgpu.cpp..."
        cat > fiction_gfx/fiction_gfx_webgpu.cpp << 'EOF'
// Fiction Graphics WebGPU Implementation
// This file just includes the header to instantiate the inline functions
#define FICTION_USE_WEBGPU
#include "fiction_gfx_webgpu.hpp"
EOF
        em++ -std=c++20 -O2 -c -fPIC \
            --use-port=emdawnwebgpu \
            -DFICTION_USE_WEBGPU -DJANK_TARGET_EMSCRIPTEN -DJANK_TARGET_WASM=1 \
            -DSTBI_NO_THREAD_LOCALS \
            -I"$JANK_DIR/include/cpp" \
            -I"$JANK_DIR/third-party" \
            -I"$JANK_DIR/third-party/bdwgc/include" \
            -I"$JANK_DIR/third-party/immer" \
            -I"$JANK_DIR/third-party/folly" \
            -I"$JANK_DIR/third-party/boost-multiprecision/include" \
            -I"$JANK_DIR/third-party/boost-preprocessor/include" \
            -I"$JANK_DIR/third-party/stduuid/include" \
            -Ifiction_gfx -Ivulkan -Ivendor -Ivendor/flecs/distr \
            fiction_gfx/fiction_gfx_webgpu.cpp \
            -o fiction_gfx/build-wasm/fiction_gfx_wasm.o
    }

cd "$JANK_DIR"

echo ""
echo "Running emscripten-bundle..."
echo ""

RELEASE=1 ./bin/emscripten-bundle -v \
    --native-obj "$SOMETHING_DIR/vendor/flecs/distr/flecs.o" \
    --native-obj "$SOMETHING_DIR/vendor/vybe/vybe_flecs_jank.o" \
    --native-lib "$SOMETHING_DIR/fiction_gfx/libfiction_gfx_stub.dylib" \
    --jit-define "FICTION_USE_STUB" \
    --prelink-lib "$SOMETHING_DIR/vendor/flecs/distr/flecs_wasm.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/vybe/vybe_flecs_jank_wasm.o" \
    --prelink-lib "$SOMETHING_DIR/fiction_gfx/build-wasm/fiction_gfx_wasm.o" \
    -I "$SOMETHING_DIR/fiction_gfx" \
    -I "$SOMETHING_DIR/vulkan" \
    -I "$SOMETHING_DIR/vendor" \
    -I "$SOMETHING_DIR/vendor/flecs/distr" \
    -I "$SOMETHING_DIR" \
    --em-flag "--use-port=emdawnwebgpu" \
    --compile-flag "-DFICTION_USE_WEBGPU" \
    --compile-flag "--use-port=emdawnwebgpu" \
    --em-flag "-sASYNCIFY" \
    --em-flag "-sFORCE_FILESYSTEM=1" \
    --em-flag "-sALLOW_MEMORY_GROWTH=1" \
    --em-flag "-sINITIAL_MEMORY=67108864" \
    --em-flag "--preload-file $SOMETHING_DIR/fonts@/fonts" \
    --em-flag "--preload-file $SOMETHING_DIR/stories@/stories" \
    --em-flag "--preload-file $SOMETHING_DIR/resources/fiction@/resources/fiction" \
    "$SOMETHING_DIR/src/fiction.jank"

# Copy HTML file from project
cp "$SOMETHING_DIR/fiction_canvas.html" "$JANK_DIR/build-wasm/fiction_canvas.html"

echo ""
echo "=== Build complete ==="
echo ""
echo "To run locally:"
echo "  cd $JANK_DIR/build-wasm"
echo "  python3 -m http.server 8888"
echo "  Open: http://localhost:8888/fiction_canvas.html"
echo ""
