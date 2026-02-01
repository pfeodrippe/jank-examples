#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building Raylib for WASM ==="

RAYLIB_DIR="vendor/raylib"
RAYLIB_SRC="$RAYLIB_DIR/src"
DISTR_DIR="$RAYLIB_DIR/distr"
OBJS_WASM_DIR="$DISTR_DIR/objs-wasm"

# Create directories
mkdir -p "$DISTR_DIR"
mkdir -p "$OBJS_WASM_DIR"

# Check if we need to rebuild (simple marker file approach)
REBUILD_MARKER="$OBJS_WASM_DIR/.build_complete"

if [ ! -f "$REBUILD_MARKER" ]; then
    echo "Building raylib with emscripten..."
    rm -f "$OBJS_WASM_DIR"/*.o 2>/dev/null || true

    # Emscripten compile flags for raylib
    # PLATFORM_WEB tells raylib to use emscripten-specific code paths
    # GRAPHICS_API_OPENGL_ES2 is required for WebGL
    CFLAGS="-O2 -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2"

    echo "Compiling rcore.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" -I"$RAYLIB_SRC/external/glfw/include" \
        -c "$RAYLIB_SRC/rcore.c" -o "$OBJS_WASM_DIR/rcore.o"

    echo "Compiling rshapes.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rshapes.c" -o "$OBJS_WASM_DIR/rshapes.o"

    echo "Compiling rtextures.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rtextures.c" -o "$OBJS_WASM_DIR/rtextures.o"

    echo "Compiling rtext.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rtext.c" -o "$OBJS_WASM_DIR/rtext.o"

    echo "Compiling rmodels.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rmodels.c" -o "$OBJS_WASM_DIR/rmodels.o"

    echo "Compiling raudio.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/raudio.c" -o "$OBJS_WASM_DIR/raudio.o"

    echo "Compiling utils.c for WASM..."
    emcc $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/utils.c" -o "$OBJS_WASM_DIR/utils.o"

    # Note: rglfw.c is not needed for WASM because emscripten provides GLFW via -sUSE_GLFW=3

    # Create the WASM static library
    echo "Creating libraylib_wasm.a..."
    emar rcs "$DISTR_DIR/libraylib_wasm.a" "$OBJS_WASM_DIR"/*.o

    touch "$REBUILD_MARKER"
    echo "Built raylib for WASM from source"
else
    echo "Using cached WASM raylib build"
fi

# Compile the jank wrapper for WASM
WRAPPER_WASM="$DISTR_DIR/raylib_jank_wrapper_wasm.o"
WRAPPER_SRC="$DISTR_DIR/raylib_jank_wrapper.cpp"

# Create wrapper source if it doesn't exist
if [ ! -f "$WRAPPER_SRC" ]; then
    echo "Creating raylib_jank_wrapper.cpp..."
    cat > "$WRAPPER_SRC" << 'WRAPPEREOF'
// raylib_jank_wrapper.cpp
// Pre-compiled wrapper that links raylib with jank runtime

#include "raylib.h"

extern "C" {

void raylib_init_window(int w, int h, const char* title) {
    InitWindow(w, h, title);
}

void raylib_close_window() {
    CloseWindow();
}

void raylib_set_fps(int fps) {
    SetTargetFPS(fps);
}

bool raylib_should_close() {
    return WindowShouldClose();
}

void raylib_begin_drawing() {
    BeginDrawing();
}

void raylib_end_drawing() {
    EndDrawing();
}

void raylib_clear(int r, int g, int b, int a) {
    ClearBackground(Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void raylib_draw_rect(int x, int y, int w, int h, int r, int g, int b, int a) {
    DrawRectangle(x, y, w, h, Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void raylib_draw_circle(int x, int y, float radius, int r, int g, int b, int a) {
    DrawCircle(x, y, radius, Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

void raylib_draw_text(const char* text, int x, int y, int size, int r, int g, int b, int a) {
    DrawText(text, x, y, size, Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a});
}

}
WRAPPEREOF
fi

# Compile wrapper for WASM if source is newer or object doesn't exist
if [ ! -f "$WRAPPER_WASM" ] || [ "$WRAPPER_SRC" -nt "$WRAPPER_WASM" ]; then
    echo "Compiling raylib_jank_wrapper.cpp for WASM..."
    em++ -c -O2 -std=c++17 \
        -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2 \
        -I"$RAYLIB_SRC" \
        "$WRAPPER_SRC" \
        -o "$WRAPPER_WASM"
else
    echo "Using cached WASM wrapper object"
fi

# Create combined library with wrapper
echo "Creating combined WASM library with wrapper..."
emar rcs "$DISTR_DIR/libraylib_jank_wasm.a" \
    "$WRAPPER_WASM" \
    "$OBJS_WASM_DIR"/*.o

echo ""
echo "=== WASM Build complete ==="
echo ""
echo "Files created:"
echo "  - $DISTR_DIR/libraylib_wasm.a (raylib only)"
echo "  - $DISTR_DIR/raylib_jank_wrapper_wasm.o (wrapper only)"
echo "  - $DISTR_DIR/libraylib_jank_wasm.a (combined)"
echo ""
echo "For emscripten-bundle, use:"
echo "  --prelink-lib vendor/raylib/distr/libraylib_wasm.a"
echo "  --prelink-lib vendor/raylib/distr/raylib_jank_wrapper_wasm.o"
