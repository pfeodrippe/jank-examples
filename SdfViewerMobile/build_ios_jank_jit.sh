#!/bin/bash
# iOS JIT-only build script using jank's ios-bundle --jit
# Builds only core libs + nREPL support - app namespaces are loaded via remote compile server
#
# Usage: ./build_ios_jank_jit.sh <target>
#   target: 'simulator' or 'device'

set -e
cd "$(dirname "$0")/.."

# Require explicit target argument
if [[ "$1" != "simulator" && "$1" != "device" ]]; then
    echo "Error: You must specify a target: 'simulator' or 'device'"
    echo "Usage: $0 <target>"
    exit 1
fi

TARGET="$1"

# Determine jank path (env var or default for local dev)
JANK_SRC="${JANK_SRC:-/Users/pfeodrippe/dev/jank/compiler+runtime}"

# Output directory (JIT mode uses different directories)
if [[ "$TARGET" == "simulator" ]]; then
    OUTPUT_DIR="SdfViewerMobile/build-iphonesimulator-jit"
else
    OUTPUT_DIR="SdfViewerMobile/build-iphoneos-jit"
fi

echo "Building iOS JIT-only bundle..."
echo "Output: $OUTPUT_DIR"
echo ""

# Build iOS JIT bundle using jank's ios-bundle --jit
# This only compiles core libs + jank_aot_init.cpp for JIT mode
# App namespaces will be loaded via remote compile server at runtime
"$JANK_SRC/bin/ios-bundle" \
  --jit \
  --output-dir "$OUTPUT_DIR" \
  "$TARGET"

echo ""
echo "JIT-only bundle complete!"
echo "Libraries in: $OUTPUT_DIR"
echo ""
echo "Remember: App namespaces are NOT bundled."
echo "They will be loaded from the remote compile server (macOS) at runtime."
echo ""
echo "Before loading app namespaces, configure the compile server:"
echo "  jank::compile_server::configure_remote_compile(\"host\", 5570);"
echo "  jank::compile_server::connect_remote_compile();"
