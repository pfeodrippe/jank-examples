#!/bin/bash
set -e

cd "$(dirname "$0")/.."

export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# Build if needed
if [ ! -f "vendor/jolt_wrapper.o" ]; then
    echo "Building Jolt wrapper..."
    bash ./build_jolt.sh
fi

# Use static library linking (like Raylib)
echo "Running JoltPhysics demo..."
echo ""

jank -L./vendor/JoltPhysics/distr \
     --jit-lib jolt_jank \
     --link-lib vendor/JoltPhysics/distr/libjolt_jank.a \
     -I./vendor/JoltPhysics \
     --module-path src \
     run-main my-jolt-static
