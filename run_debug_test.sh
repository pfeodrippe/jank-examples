#!/bin/bash
set -e

cd "$(dirname "$0")"

export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

SOMETHING_DIR="/Users/pfeodrippe/dev/something"

# Check for --lldb flag
USE_LLDB=false
if [ "$1" = "--lldb" ]; then
    USE_LLDB=true
    shift
fi

echo "Running debug test..."
echo ""

JANK_ARGS=(
    --debug
    -L"$SOMETHING_DIR/vendor/raylib/distr"
    --jit-lib raylib_jank
    -I./vendor/raylib/distr
    -I./vendor/raylib/src
    --link-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib_jank.a"
    --framework Cocoa
    --framework IOKit
    --framework OpenGL
    --framework CoreVideo
    --framework CoreFoundation
    --module-path src
    run-main debug-test
)

if [ "$USE_LLDB" = true ]; then
    echo "Using lldb with JIT loader enabled..."
    echo "This will show jank source locations in stack traces!"
    echo ""

    # Create lldb commands file
    cat > /tmp/lldb_commands.txt << 'EOF'
settings set plugin.jit-loader.gdb.enable on
breakpoint set -n __cxa_throw
run
bt
quit
EOF

    lldb -s /tmp/lldb_commands.txt -- jank "${JANK_ARGS[@]}"
else
    jank "${JANK_ARGS[@]}"
fi
