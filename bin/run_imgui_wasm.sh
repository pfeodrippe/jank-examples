#!/bin/bash
set -e

cd "$(dirname "$0")/.."

JANK_DIR="/Users/pfeodrippe/dev/jank/compiler+runtime"
SOMETHING_DIR="/Users/pfeodrippe/dev/something"

# Build ImGui for WASM if needed
if [ ! -f "vendor/imgui/build-wasm/imgui.o" ]; then
    echo "Building ImGui for WASM..."
    BUILD_WASM=1 bash ./build_imgui.sh
fi

cd "$JANK_DIR"

echo "=== Building ImGui WASM demo ==="
echo ""

# Collect ImGui object files as prelink-lib flags
IMGUI_LIBS=""
for f in "$SOMETHING_DIR/vendor/imgui/build-wasm"/*.o; do
    IMGUI_LIBS="$IMGUI_LIBS --prelink-lib $f"
done

RELEASE=1 ./bin/emscripten-bundle --skip-build \
    -L "$SOMETHING_DIR/vendor/raylib/distr" \
    --native-lib raylib_jank \
    --native-lib imgui \
    --prelink-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib.web.a" \
    --prelink-lib "$SOMETHING_DIR/vendor/raylib/distr/raylib_jank_wrapper_wasm.o" \
    $IMGUI_LIBS \
    -I "$SOMETHING_DIR/vendor/raylib/distr" \
    -I "$SOMETHING_DIR/vendor/imgui" \
    --em-flag "-sUSE_GLFW=3" \
    --em-flag "-sASYNCIFY" \
    --em-flag "-sFORCE_FILESYSTEM=1" \
    "$SOMETHING_DIR/src/my_imgui_static.jank"

# Copy the canvas HTML (required for raylib/GLFW browser setup)
cp "$SOMETHING_DIR/src/my_imgui_static_canvas.html" "$JANK_DIR/build-wasm/"

echo ""
echo "=== Build complete ==="
echo "Open in browser: http://localhost:8888/my_imgui_static_canvas.html"
