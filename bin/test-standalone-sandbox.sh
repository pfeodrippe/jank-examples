#!/bin/bash
# Test a jank standalone app to simulate running on another machine
# Usage: ./test-standalone-sandbox.sh <app-path> [args...]
#
# This script temporarily hides development paths that won't exist on
# another machine, helping catch hardcoded path issues before distribution.

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <app-path> [args...]"
    echo ""
    echo "Examples:"
    echo "  $0 SDFViewer.app"
    echo "  $0 ./MyApp.app/Contents/MacOS/MyApp"
    echo ""
    echo "This script temporarily hides these paths:"
    echo "  - ~/dev/jank -> ~/dev/jank.hidden"
    echo ""
    echo "This simulates running on a fresh Mac without jank dev install."
    exit 1
fi

APP_PATH="$1"
shift

# Resolve to absolute path
if [[ "$APP_PATH" == *.app ]]; then
    # It's an app bundle, use the launcher
    APP_PATH="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")/Contents/MacOS/$(basename "$APP_PATH" .app)"
fi
APP_PATH="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")"

if [ ! -x "$APP_PATH" ]; then
    echo "Error: $APP_PATH is not executable"
    exit 1
fi

# Paths to hide during testing
JANK_PATH="$HOME/dev/jank"
JANK_HIDDEN="$HOME/dev/jank.hidden"

# Cleanup function to restore paths
cleanup() {
    if [ -d "$JANK_HIDDEN" ]; then
        echo ""
        echo "Restoring $JANK_PATH..."
        mv "$JANK_HIDDEN" "$JANK_PATH"
    fi
}

# Set trap to ensure cleanup runs on exit
trap cleanup EXIT INT TERM

echo "============================================"
echo "Testing standalone app (simulating fresh Mac)"
echo "============================================"
echo "App: $APP_PATH"
echo ""

# Hide jank development path
if [ -d "$JANK_PATH" ]; then
    echo "Hiding $JANK_PATH..."
    mv "$JANK_PATH" "$JANK_HIDDEN"
else
    echo "Note: $JANK_PATH not found, skipping hide"
fi

echo ""
echo "Hidden paths:"
echo "  - $JANK_PATH"
echo ""
echo "Starting app..."
echo "============================================"
echo ""

# Run the app (cleanup will run automatically on exit)
"$APP_PATH" "$@"
