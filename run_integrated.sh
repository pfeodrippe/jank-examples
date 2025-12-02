#!/bin/bash
set -e

cd "$(dirname "$0")"

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

# Collect object files
OBJ_ARGS=""

# ImGui objects
for f in vendor/imgui/build/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Jolt wrapper + Jolt objects
OBJ_ARGS="$OBJ_ARGS --obj vendor/jolt_wrapper.o"
for f in vendor/JoltPhysics/distr/objs/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Raylib dylib (includes Cocoa frameworks)
OBJ_ARGS="$OBJ_ARGS --lib vendor/raylib/distr/libraylib_jank.dylib"

echo "Running integrated demo (Raylib + ImGui + Jolt)..."
echo ""

# Run with include paths for all libraries
jank $OBJ_ARGS \
    -I./vendor/imgui \
    -I./vendor/raylib/src \
    -I./vendor/JoltPhysics \
    --module-path src \
    run-main my-integrated-demo -main
