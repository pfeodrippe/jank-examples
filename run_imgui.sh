#!/bin/bash
set -e

cd "$(dirname "$0")"

export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# Build ImGui if needed
if [ ! -f "vendor/imgui/build/imgui.o" ]; then
    echo "Building ImGui..."
    bash build_imgui.sh
fi

# Collect object files
OBJ_ARGS=""

# ImGui objects
for f in vendor/imgui/build/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Raylib dylib (includes Cocoa frameworks)
OBJ_ARGS="$OBJ_ARGS --lib vendor/raylib/distr/libraylib_jank.dylib"

echo "Running ImGui demo..."
echo ""

# Run with include paths for both imgui and raylib
jank $OBJ_ARGS \
    -I./vendor/imgui \
    -I./vendor/rlImGui \
    -I./vendor/raylib/src \
    --module-path src \
    run-main my-imgui-static -main
