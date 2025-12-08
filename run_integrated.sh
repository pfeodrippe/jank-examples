#!/bin/bash
set -e

cd "$(dirname "$0")"

# Check for flags
USE_LLDB=false
USE_DEBUG=false
SAVE_CPP=false
USE_PROFILE=false
USE_PROFILE_FNS=false
USE_XTRACE=false
while [[ "$1" == --* ]]; do
    case "$1" in
        --lldb)
            USE_LLDB=true
            shift
            ;;
        --debug)
            USE_DEBUG=true
            shift
            ;;
        --save-cpp)
            SAVE_CPP=true
            shift
            ;;
        --profile)
            USE_PROFILE=true
            shift
            ;;
        --profile-fns)
            USE_PROFILE_FNS=true
            shift
            ;;
        --xtrace)
            USE_XTRACE=true
            shift
            ;;
        *)
            break
            ;;
    esac
done

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

# Build jank arguments array
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
    --module-path src
)

# Add debug flag if requested
if [ "$USE_DEBUG" = true ]; then
    echo "Debug mode enabled - generating debug symbols"
    JANK_ARGS+=(--debug)
fi

# Add save-cpp flag if requested
if [ "$SAVE_CPP" = true ]; then
    echo "Saving generated C++ to ./generated_cpp/"
    mkdir -p generated_cpp
    JANK_ARGS+=(--save-cpp --save-cpp-path ./generated_cpp)
fi

# Add profile flag if requested
if [ "$USE_PROFILE" = true ]; then
    echo "Profiling enabled - output will be written to jank.profile"
    JANK_ARGS+=(--profile)
fi

# Add profile-fns flag if requested (automatically profiles all functions)
if [ "$USE_PROFILE_FNS" = true ]; then
    echo "Function profiling enabled - all functions will be automatically profiled"
    JANK_ARGS+=(--profile-fns)
fi

JANK_ARGS+=(run-main my-integrated-demo -main)

# Run with appropriate tool
if [ "$USE_LLDB" = true ]; then
    # Enable debug symbols for lldb (required for source mapping)
    JANK_ARGS=(--debug "${JANK_ARGS[@]}")

    echo "Running with lldb debugger (JIT loader enabled)..."
    echo "This will show jank source locations in stack traces!"
    echo ""

    # Create lldb commands file with JIT loader settings
    cat > /tmp/lldb_commands.txt << 'EOF'
settings set plugin.jit-loader.gdb.enable on
breakpoint set -n __cxa_throw
run
bt
EOF

    lldb -s /tmp/lldb_commands.txt -- jank "${JANK_ARGS[@]}"
elif [ "$USE_XTRACE" = true ]; then
    TRACE_FILE="something_$(date +%Y%m%d_%H%M%S).trace"
    echo "Running with xctrace (Time Profiler)..."
    echo "Trace will be saved to: $TRACE_FILE"
    echo "Open with: open $TRACE_FILE"
    echo ""

    xctrace record --template 'Time Profiler' --output "$TRACE_FILE" --launch -- jank "${JANK_ARGS[@]}"

    echo ""
    echo "Trace recorded: $TRACE_FILE"
    echo "Run 'open $TRACE_FILE' to view in Instruments"
else
    jank "${JANK_ARGS[@]}"
fi

# If profiling was enabled, show analysis hint
if ([ "$USE_PROFILE" = true ] || [ "$USE_PROFILE_FNS" = true ]) && [ -f "jank.profile" ]; then
    echo ""
    echo "Profile data written to jank.profile"
    echo "Analyze with: /Users/pfeodrippe/dev/jank/compiler+runtime/bin/profile-analyze jank.profile"
fi
