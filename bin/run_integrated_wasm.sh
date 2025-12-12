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

# Build Jolt for WASM if needed
if [ ! -f "vendor/jolt_wrapper_wasm.o" ]; then
    echo "Building Jolt for WASM..."
    bash ./build_jolt_wasm.sh
fi

# Check if Flecs WASM exists
if [ ! -f "vendor/flecs/distr/flecs_wasm.o" ]; then
    echo "ERROR: vendor/flecs/distr/flecs_wasm.o not found!"
    echo "Please build Flecs for WASM first."
    exit 1
fi

cd "$JANK_DIR"

echo "=== Building Integrated Demo WASM (Raylib + ImGui + Jolt + Flecs) ==="
echo ""

# Collect ImGui object files as prelink-lib flags
IMGUI_LIBS=""
for f in "$SOMETHING_DIR/vendor/imgui/build-wasm"/*.o; do
    IMGUI_LIBS="$IMGUI_LIBS --prelink-lib $f"
done

# Collect native Jolt object files for JIT during AOT
JOLT_NATIVE_OBJS=""
JOLT_NATIVE_OBJS="$JOLT_NATIVE_OBJS --native-obj $SOMETHING_DIR/vendor/jolt_wrapper.o"
for f in "$SOMETHING_DIR/vendor/JoltPhysics/distr/objs"/*.o; do
    JOLT_NATIVE_OBJS="$JOLT_NATIVE_OBJS --native-obj $f"
done

# Collect native ImGui object files for JIT during AOT
IMGUI_NATIVE_OBJS=""
for f in "$SOMETHING_DIR/vendor/imgui/build"/*.o; do
    IMGUI_NATIVE_OBJS="$IMGUI_NATIVE_OBJS --native-obj $f"
done

RELEASE=1 ./bin/emscripten-bundle --skip-build \
    -L "$SOMETHING_DIR/vendor/raylib/distr" \
    --native-lib raylib_jank \
    $IMGUI_NATIVE_OBJS \
    $JOLT_NATIVE_OBJS \
    --native-obj "$SOMETHING_DIR/vendor/flecs/distr/flecs.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib.web.a" \
    --prelink-lib "$SOMETHING_DIR/vendor/raylib/distr/raylib_jank_wrapper_wasm.o" \
    $IMGUI_LIBS \
    --prelink-lib "$SOMETHING_DIR/vendor/jolt_wrapper_wasm.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/jolt_combined_wasm.a" \
    --prelink-lib "$SOMETHING_DIR/vendor/flecs/distr/flecs_wasm.o" \
    -I "$SOMETHING_DIR/vendor/raylib/distr" \
    -I "$SOMETHING_DIR/vendor/raylib/src" \
    -I "$SOMETHING_DIR/vendor/imgui" \
    -I "$SOMETHING_DIR/vendor/JoltPhysics" \
    -I "$SOMETHING_DIR/vendor/flecs/distr" \
    --em-flag "-sUSE_GLFW=3" \
    --em-flag "-sASYNCIFY" \
    --em-flag "-sFORCE_FILESYSTEM=1" \
    --em-flag "-sALLOW_MEMORY_GROWTH=1" \
    --em-flag "-sINITIAL_MEMORY=67108864" \
    "$SOMETHING_DIR/src/my_integrated_demo.jank"

# Copy the canvas HTML (required for raylib/GLFW browser setup)
cp "$SOMETHING_DIR/src/my_integrated_demo_canvas.html" "$JANK_DIR/build-wasm/"

echo ""
echo "=== Build complete ==="
echo "Open in browser: http://localhost:8888/my_integrated_demo_canvas.html"
