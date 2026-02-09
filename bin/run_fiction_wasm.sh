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

# Build miniaudio objects for native JIT + WASM prelink if needed
if [ ! -f "vendor/vybe/miniaudio.o" ]; then
    echo "Building miniaudio native object..."
    make vendor/vybe/miniaudio.o
fi
if [ ! -f "vendor/vybe/miniaudio_wasm.o" ]; then
    echo "Building miniaudio WASM object..."
    make build-miniaudio-wasm
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

# Stage fiction resources for WASM embed without WAV files.
# Runtime will prefer WAV first, then fallback to MP3, so excluding WAV
# keeps bundles smaller while preserving playback via MP3 assets.
EMBED_RES_DIR="$JANK_DIR/build-wasm/embed_resources"
EMBED_FICTION_DIR="$EMBED_RES_DIR/fiction"
rm -rf "$EMBED_FICTION_DIR"
mkdir -p "$EMBED_RES_DIR"
cp -R "$SOMETHING_DIR/resources/fiction" "$EMBED_FICTION_DIR"

# Ensure MP3 voice assets exist for WASM by transcoding source WAV files.
# Existing MP3 files are kept; WAV files are removed before embedding.
if command -v ffmpeg >/dev/null 2>&1; then
    while IFS= read -r src_wav; do
        rel_path="${src_wav#"$SOMETHING_DIR/resources/fiction/"}"
        rel_no_ext="${rel_path%.*}"
        dst_mp3="$EMBED_FICTION_DIR/$rel_no_ext.mp3"
        mkdir -p "$(dirname "$dst_mp3")"
        if [ ! -f "$dst_mp3" ] || [ "$src_wav" -nt "$dst_mp3" ]; then
            ffmpeg -hide_banner -loglevel error -y -i "$src_wav" \
                -ac 1 -ar 48000 -codec:a libmp3lame -q:a 4 "$dst_mp3"
        fi
    done < <(find "$SOMETHING_DIR/resources/fiction" -type f -iname '*.wav')
else
    echo "WARN: ffmpeg not found; WAV voice assets will not be transcoded to MP3 for WASM."
fi

find "$EMBED_FICTION_DIR" -type f -iname '*.wav' -delete

RELEASE=1 ./bin/emscripten-bundle -v \
    --native-obj "$SOMETHING_DIR/vendor/flecs/distr/flecs.o" \
    --native-obj "$SOMETHING_DIR/vendor/vybe/vybe_flecs_jank.o" \
    --native-obj "$SOMETHING_DIR/vendor/vybe/miniaudio.o" \
    --native-lib "$SOMETHING_DIR/fiction_gfx/libfiction_gfx_stub.dylib" \
    --jit-define "FICTION_USE_STUB" \
    --prelink-lib "$SOMETHING_DIR/vendor/flecs/distr/flecs_wasm.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/vybe/vybe_flecs_jank_wasm.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/vybe/miniaudio_wasm.o" \
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
    --em-flag "--embed-file $SOMETHING_DIR/fonts@/fonts" \
    --em-flag "--embed-file $SOMETHING_DIR/stories@/stories" \
    --em-flag "--embed-file $EMBED_FICTION_DIR@/resources/fiction" \
    --em-flag "--embed-file $SOMETHING_DIR/vulkan_fiction/particle.wgsl@/vulkan_fiction/particle.wgsl" \
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
