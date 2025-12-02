#!/bin/bash
set -e

cd "$(dirname "$0")"

# Build WASM if needed
if [ ! -f "vendor/jolt_wasm.o" ]; then
    echo "Building Jolt WASM..."
    bash build_jolt_wasm.sh
fi

echo "Running JoltPhysics WASM demo..."
echo ""

cd /Users/pfeodrippe/dev/jank/compiler+runtime
./bin/emscripten-bundle --skip-build --run \
    --native-obj /Users/pfeodrippe/dev/something/vendor/jolt_wrapper.o \
    $(for f in /Users/pfeodrippe/dev/something/vendor/JoltPhysics/distr/objs/*.o; do echo "--native-obj $f"; done) \
    --lib /Users/pfeodrippe/dev/something/vendor/jolt_wasm.o \
    -I /Users/pfeodrippe/dev/something/vendor/JoltPhysics \
    /Users/pfeodrippe/dev/something/src/my_jolt_static.jank
