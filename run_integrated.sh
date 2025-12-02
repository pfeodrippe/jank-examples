#!/bin/bash
set -e

cd "$(dirname "$0")"

export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# Build ImGui if needed
if [ ! -f "vendor/imgui/build/imgui.o" ]; then
    echo "Building ImGui..."
    bash build_imgui.sh
fi

# Build Jolt if needed
if [ ! -f "vendor/jolt_wrapper.o" ]; then
    echo "Building Jolt wrapper..."
    bash build_jolt.sh
fi

echo "Running integrated demo (Raylib + ImGui + Jolt + Flecs) - Static Linking"
echo ""

SOMETHING_DIR="/Users/pfeodrippe/dev/something"

# Collect ImGui object files (using --obj like run_imgui.sh)
OBJ_ARGS=""
for f in vendor/imgui/build/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Collect Jolt object files (using --obj like run_jolt.sh)
OBJ_ARGS="$OBJ_ARGS --obj vendor/jolt_wrapper.o"
for f in vendor/JoltPhysics/distr/objs/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Add Flecs object files
OBJ_ARGS="$OBJ_ARGS --obj vendor/flecs/distr/flecs.o"
OBJ_ARGS="$OBJ_ARGS --obj vendor/flecs/distr/flecs_jank_wrapper_native.o"

# Use static raylib with --jit-lib and --link-lib (like user's example)
jank \
    -L"$SOMETHING_DIR/vendor/raylib/distr" \
    --jit-lib raylib_jank \
    -I./vendor/raylib/distr \
    -I./vendor/raylib/src \
    -I./vendor/imgui \
    -I./vendor/JoltPhysics \
    -I./vendor/flecs/distr \
    --link-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib_jank.a" \
    $OBJ_ARGS \
    --framework Cocoa \
    --framework IOKit \
    --framework OpenGL \
    --framework CoreVideo \
    --framework CoreFoundation \
    --module-path src \
    run-main my-integrated-demo -main
