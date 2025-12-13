#!/bin/bash
set -e

cd "$(dirname "$0")/.."

# Determine jank paths - support both local dev and CI environments
if [ -d "/Users/pfeodrippe/dev/jank/compiler+runtime" ]; then
    JANK_SRC="/Users/pfeodrippe/dev/jank/compiler+runtime"
elif [ -d "$HOME/jank/compiler+runtime" ]; then
    JANK_SRC="$HOME/jank/compiler+runtime"
else
    echo "Error: Could not find jank source directory"
    exit 1
fi

# Use jank's built clang (required for header compatibility)
JANK_CXX="$JANK_SRC/build/llvm-install/usr/local/bin/clang++"
if [ ! -f "$JANK_CXX" ]; then
    echo "Error: Could not find jank's clang at $JANK_CXX"
    exit 1
fi

# Set up environment
if [ "$(uname)" = "Darwin" ]; then
    export SDKROOT=${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}
fi
export PATH="$JANK_SRC/build:$PATH"

SOMETHING_DIR="$(pwd)"

echo "Running vybe tests"
echo "  JANK_SRC: $JANK_SRC"
echo "  JANK_CXX: $JANK_CXX"
echo ""

# Build ImGui if needed
if [ ! -f "vendor/imgui/build/imgui.o" ]; then
    echo "Building ImGui..."
    bash ./build_imgui.sh
fi

# Build Jolt if needed
if [ ! -f "vendor/jolt_wrapper.o" ]; then
    echo "Building Jolt wrapper..."
    bash ./build_jolt.sh
fi

# Build vybe_flecs_jank.o if needed (must use jank's clang for header compatibility)
if [ ! -f "vendor/vybe/vybe_flecs_jank.o" ]; then
    echo "Building vybe_flecs_jank..."
    "$JANK_CXX" -c vendor/vybe/vybe_flecs_jank.cpp -o vendor/vybe/vybe_flecs_jank.o \
        -DIMMER_HAS_LIBGC=1 \
        -I$JANK_SRC/include/cpp \
        -I$JANK_SRC/third-party \
        -I$JANK_SRC/third-party/bdwgc/include \
        -I$JANK_SRC/third-party/immer \
        -I$JANK_SRC/third-party/bpptree/include \
        -I$JANK_SRC/third-party/folly \
        -I$JANK_SRC/third-party/boost-multiprecision/include \
        -I$JANK_SRC/third-party/boost-preprocessor/include \
        -I$JANK_SRC/build/llvm-install/usr/local/include \
        -Ivendor -Ivendor/flecs/distr \
        -std=c++20 -fPIC
fi

# Collect ImGui object files
OBJ_ARGS=""
for f in vendor/imgui/build/*.o; do
    [ -f "$f" ] && OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Collect Jolt object files
OBJ_ARGS="$OBJ_ARGS --obj vendor/jolt_wrapper.o"
for f in vendor/JoltPhysics/distr/objs/*.o; do
    [ -f "$f" ] && OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Add Flecs object files
OBJ_ARGS="$OBJ_ARGS --obj vendor/flecs/distr/flecs.o"
OBJ_ARGS="$OBJ_ARGS --obj vendor/flecs/distr/flecs_jank_wrapper_native.o"
OBJ_ARGS="$OBJ_ARGS --obj vendor/vybe/vybe_flecs_jank.o"

# Build jank arguments
JANK_ARGS=(
    -L"$SOMETHING_DIR/vendor/raylib/distr"
    --jit-lib raylib_jank
    -I./vendor/raylib/distr
    -I./vendor/raylib/src
    -I./vendor/imgui
    -I./vendor/JoltPhysics
    -I./vendor/flecs/distr
    -I./vendor
    --link-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib_jank.a"
    $OBJ_ARGS
    --framework Cocoa
    --framework IOKit
    --framework OpenGL
    --framework CoreVideo
    --framework CoreFoundation
    --module-path src:test
    run-main vybe.flecs-test -main
)

echo "=== Running vybe.flecs-test ==="
jank "${JANK_ARGS[@]}"

echo ""
echo "=== Running vybe.type-test ==="
# Reuse same args but change the module to run
JANK_TYPE_ARGS=(
    -L"$SOMETHING_DIR/vendor/raylib/distr"
    --jit-lib raylib_jank
    -I./vendor/raylib/distr
    -I./vendor/raylib/src
    -I./vendor/imgui
    -I./vendor/JoltPhysics
    -I./vendor/flecs/distr
    -I./vendor
    --link-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib_jank.a"
    $OBJ_ARGS
    --framework Cocoa
    --framework IOKit
    --framework OpenGL
    --framework CoreVideo
    --framework CoreFoundation
    --module-path src:test
    run-main vybe.type-test -main
)
jank "${JANK_TYPE_ARGS[@]}"

echo ""
echo "All tests passed!"
