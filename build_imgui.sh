#!/bin/bash
set -e

cd "$(dirname "$0")"

BUILD_WASM=${BUILD_WASM:-0}

if [ "$BUILD_WASM" = "1" ]; then
    echo "=== Building ImGui for WASM ==="
    CXX="em++"
    OUTPUT_DIR="vendor/imgui/build-wasm"
    EXTRA_FLAGS=""
else
    echo "=== Building ImGui (native) ==="
    # Use jank's compiler for ABI compatibility, fallback to system clang
    CXX="${CXX:-clang++}"
    OUTPUT_DIR="vendor/imgui/build"
    EXTRA_FLAGS="-fPIC"
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Compile ImGui core files
echo "Compiling ImGui core..."
$CXX -c -O2 -std=c++17 -DNDEBUG $EXTRA_FLAGS \
    -I vendor/imgui \
    vendor/imgui/imgui.cpp \
    -o "$OUTPUT_DIR/imgui.o"

$CXX -c -O2 -std=c++17 -DNDEBUG $EXTRA_FLAGS \
    -I vendor/imgui \
    vendor/imgui/imgui_draw.cpp \
    -o "$OUTPUT_DIR/imgui_draw.o"

$CXX -c -O2 -std=c++17 -DNDEBUG $EXTRA_FLAGS \
    -I vendor/imgui \
    vendor/imgui/imgui_tables.cpp \
    -o "$OUTPUT_DIR/imgui_tables.o"

$CXX -c -O2 -std=c++17 -DNDEBUG $EXTRA_FLAGS \
    -I vendor/imgui \
    vendor/imgui/imgui_widgets.cpp \
    -o "$OUTPUT_DIR/imgui_widgets.o"

$CXX -c -O2 -std=c++17 -DNDEBUG $EXTRA_FLAGS \
    -I vendor/imgui \
    vendor/imgui/imgui_demo.cpp \
    -o "$OUTPUT_DIR/imgui_demo.o"

if [ "$BUILD_WASM" != "1" ]; then
    # Compile rlImGui (raylib integration) - native only
    echo "Compiling rlImGui..."
    $CXX -c -O2 -std=c++17 -DNDEBUG -fPIC \
        -DNO_FONT_AWESOME \
        -I vendor/imgui \
        -I vendor/raylib/src \
        vendor/rlImGui/rlImGui.cpp \
        -o "$OUTPUT_DIR/rlImGui.o"

    # Create dylib for JIT compilation
    echo "Creating dylib..."
    $CXX -shared -o "$OUTPUT_DIR/libimgui.dylib" \
        "$OUTPUT_DIR/imgui.o" \
        "$OUTPUT_DIR/imgui_draw.o" \
        "$OUTPUT_DIR/imgui_tables.o" \
        "$OUTPUT_DIR/imgui_widgets.o" \
        "$OUTPUT_DIR/imgui_demo.o"
fi

echo ""
echo "=== Build complete ==="
echo ""
echo "Object files created in $OUTPUT_DIR/"
ls -la "$OUTPUT_DIR"/*.o
if [ "$BUILD_WASM" != "1" ]; then
    ls -la "$OUTPUT_DIR"/*.dylib
fi
