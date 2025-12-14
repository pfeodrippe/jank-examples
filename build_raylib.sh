#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building Raylib (jank wrapper) ==="

RAYLIB_DIR="vendor/raylib"
DISTR_DIR="$RAYLIB_DIR/distr"
OBJS_DIR="$DISTR_DIR/objs"

# Create distr directory if needed
mkdir -p "$DISTR_DIR"
mkdir -p "$OBJS_DIR"

# Build raylib from source if libraylib.a doesn't exist
if [ ! -f "$DISTR_DIR/libraylib.a" ]; then
    echo "Building raylib from source..."

    RAYLIB_SRC="$RAYLIB_DIR/src"

    # Compile raylib source files
    # Using PLATFORM_DESKTOP and GRAPHICS_API_OPENGL_33 for macOS
    CFLAGS="-O2 -fPIC -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33"

    echo "Compiling rcore.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" -I"$RAYLIB_SRC/external/glfw/include" \
        -c "$RAYLIB_SRC/rcore.c" -o "$OBJS_DIR/rcore.o"

    echo "Compiling rshapes.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rshapes.c" -o "$OBJS_DIR/rshapes.o"

    echo "Compiling rtextures.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rtextures.c" -o "$OBJS_DIR/rtextures.o"

    echo "Compiling rtext.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rtext.c" -o "$OBJS_DIR/rtext.o"

    echo "Compiling rmodels.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/rmodels.c" -o "$OBJS_DIR/rmodels.o"

    echo "Compiling raudio.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/raudio.c" -o "$OBJS_DIR/raudio.o"

    echo "Compiling utils.c..."
    clang $CFLAGS -I"$RAYLIB_SRC" \
        -c "$RAYLIB_SRC/utils.c" -o "$OBJS_DIR/utils.o"

    echo "Compiling rglfw.c (GLFW integration)..."
    clang $CFLAGS -I"$RAYLIB_SRC" -I"$RAYLIB_SRC/external/glfw/include" \
        -D_GLFW_COCOA -x objective-c \
        -c "$RAYLIB_SRC/rglfw.c" -o "$OBJS_DIR/rglfw.o"

    # Create static library
    echo "Creating libraylib.a..."
    ar rcs "$DISTR_DIR/libraylib.a" "$OBJS_DIR"/*.o
    echo "Built libraylib.a from source"

elif [ ! -d "$OBJS_DIR" ] || [ -z "$(ls -A $OBJS_DIR/*.o 2>/dev/null)" ]; then
    # Extract object files from existing libraylib.a if objs don't exist
    echo "Extracting object files from libraylib.a..."
    cd "$OBJS_DIR"
    ar -x ../libraylib.a
    cd ../../..
    echo "Extracted object files"
fi

# Copy raylib.h to distr if needed (for wrapper compilation)
if [ ! -f "$DISTR_DIR/raylib.h" ]; then
    cp "$RAYLIB_DIR/src/raylib.h" "$DISTR_DIR/"
fi

# Create wrapper source if it doesn't exist
if [ ! -f "$DISTR_DIR/raylib_jank_wrapper.cpp" ]; then
    echo "Creating raylib_jank_wrapper.cpp..."
    cat > "$DISTR_DIR/raylib_jank_wrapper.cpp" << 'WRAPPEREOF'
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

# Compile the jank wrapper
echo "Compiling raylib_jank_wrapper.cpp..."
clang++ -c -O2 -std=c++17 -fPIC \
    -I"$RAYLIB_DIR/src" \
    "$DISTR_DIR/raylib_jank_wrapper.cpp" \
    -o "$DISTR_DIR/raylib_jank_wrapper.o"

# Create static library by combining wrapper with raylib objects
echo "Creating libraylib_jank.a..."
ar rcs "$DISTR_DIR/libraylib_jank.a" \
    "$DISTR_DIR/raylib_jank_wrapper.o" \
    "$OBJS_DIR"/*.o

# Create dynamic library (macOS only for now)
if [ "$(uname)" = "Darwin" ]; then
    echo "Creating libraylib_jank.dylib..."
    clang++ -shared -o "$DISTR_DIR/libraylib_jank.dylib" \
        "$DISTR_DIR/raylib_jank_wrapper.o" \
        "$OBJS_DIR"/*.o \
        -framework Cocoa \
        -framework IOKit \
        -framework OpenGL \
        -framework CoreVideo \
        -framework CoreFoundation

    # Create symlink for jank --jit-lib
    ln -sf libraylib_jank.dylib "$DISTR_DIR/raylib_jank.dylib"
fi

echo ""
echo "=== Build complete ==="
echo "  - $DISTR_DIR/libraylib_jank.a (static library)"
if [ "$(uname)" = "Darwin" ]; then
    echo "  - $DISTR_DIR/libraylib_jank.dylib (dynamic library)"
fi
