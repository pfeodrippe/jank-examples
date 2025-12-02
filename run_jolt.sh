#!/bin/bash
set -e

cd "$(dirname "$0")"

export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# Build if needed
if [ ! -f "vendor/jolt_wrapper.o" ]; then
    echo "Building Jolt wrapper..."
    bash build_jolt.sh
fi

# Collect all object files - wrapper first, then Jolt
OBJ_ARGS="--obj vendor/jolt_wrapper.o"
for f in vendor/JoltPhysics/distr/objs/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

echo "Running JoltPhysics demo..."
echo ""

jank $OBJ_ARGS -I./vendor/JoltPhysics --module-path src run-main my-jolt-static -main
