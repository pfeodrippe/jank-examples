#!/bin/bash
# Test a jank standalone app to verify it's self-contained behaviour
# Uses macOS sandbox-exec for true process isolation
# Usage: ./test-standalone-sandbox.sh <app-path> [args...]

set -e

ALLOW_SDK=true
ALLOW_NETWORK=false

while [[ "$1" == --* ]]; do
    case "$1" in
        --no-sdk|--strict)
            ALLOW_SDK=false
            shift
            ;;
        --allow-network)
            ALLOW_NETWORK=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

if [ -z "$1" ]; then
    echo "Usage: $0 [options] <app-path> [args...]"
    echo ""
    echo "Options:"
    echo "  --no-sdk         Deny SDK headers (test on bare Mac without CLT)"
    echo "  --allow-network  Allow network access"
    echo ""
    echo "Examples:"
    echo "  $0 SDFViewer.app              # Typical end-user Mac test"
    echo "  $0 --no-sdk SDFViewer.app     # Bare Mac test (no CommandLineTools)"
    echo "  $0 --allow-network MyApp.app  # Allow network too"
    echo ""
    echo "Default behavior simulates a typical end-user Mac:"
    echo "  - SDK headers (CommandLineTools): ALLOWED"
    echo "  - Network: DENIED"
    echo "  - /opt/homebrew, /usr/local: DENIED"
    echo "  - ~/.jank, ~/.clojure, ~/.m2: DENIED"
    echo ""
    echo "Debug sandbox denials with:"
    echo "  log stream --style compact --predicate 'sender==\"Sandbox\"'"
    exit 1
fi

APP_PATH="$1"
shift

# Resolve to absolute path
if [[ "$APP_PATH" == *.app ]]; then
    # It's an app bundle, get the bundle root
    APP_BUNDLE="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")"
    APP_PATH="$APP_BUNDLE/Contents/MacOS/$(basename "$APP_PATH" .app)"
else
    APP_BUNDLE=""
fi
APP_PATH="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")"

if [ ! -x "$APP_PATH" ]; then
    echo "Error: $APP_PATH is not executable"
    exit 1
fi

# Create a temporary sandbox profile
SANDBOX_PROFILE=$(mktemp /tmp/sandbox-XXXXXX.sb)
trap "rm -f $SANDBOX_PROFILE" EXIT

# Build the sandbox profile
cat > "$SANDBOX_PROFILE" << 'SANDBOX_EOF'
(version 1)

; Deny everything by default
(deny default)

; === PROCESS ===
(allow process-fork)
(allow process-exec)
(allow signal (target self))

; === FILE SYSTEM ===
; Metadata needed for std::filesystem::canonical
(allow file-read-metadata)

; System libraries and frameworks
(allow file-read*
    (literal "/")
    (subpath "/System")
    (subpath "/usr/lib")
    (subpath "/usr/bin")
    (subpath "/bin")
    (subpath "/usr/share")
    (literal "/dev/null")
    (literal "/dev/urandom")
    (literal "/dev/random"))

; Temp directories
(allow file-read* file-write*
    (subpath "/tmp")
    (subpath "/private/tmp")
    (regex #"^/private/var/folders/"))

; === IPC / SYSTEM ===
(allow sysctl-read)
(allow mach-lookup)
(allow ipc-posix-shm)

; === GPU (for GUI apps) ===
(allow iokit-open)
(allow file-read* (subpath "/Library/GPUBundles"))

; === DENY DEV TOOLS ===
(deny file-read* file-write*
    (subpath "/opt/homebrew")
    (subpath "/usr/local")
    (subpath "/nix")
    (regex #".*/\.jank")
    (regex #".*/\.clojure")
    (regex #".*/\.m2")
    (regex #".*/\.gradle"))

SANDBOX_EOF

# SDK access (Xcode + CommandLineTools) - default ON
if [ "$ALLOW_SDK" = true ]; then
    echo "(allow file-read* (subpath \"/Applications/Xcode.app\"))" >> "$SANDBOX_PROFILE"
    echo "(allow file-read* (subpath \"/Library/Developer/CommandLineTools\"))" >> "$SANDBOX_PROFILE"
fi

# Network access - default OFF
if [ "$ALLOW_NETWORK" = true ]; then
    echo "(allow network*)" >> "$SANDBOX_PROFILE"
else
    echo "(deny network*)" >> "$SANDBOX_PROFILE"
fi

# App bundle access
if [ -n "$APP_BUNDLE" ]; then
    echo "(allow file-read* file-write* (subpath \"$APP_BUNDLE\"))" >> "$SANDBOX_PROFILE"
else
    APP_DIR="$(dirname "$APP_PATH")"
    echo "(allow file-read* file-write* (subpath \"$APP_DIR\"))" >> "$SANDBOX_PROFILE"
fi

# TMPDIR if set
if [ -n "$TMPDIR" ]; then
    echo "(allow file-read* file-write* (subpath \"$TMPDIR\"))" >> "$SANDBOX_PROFILE"
fi

# Allow current working directory (for shell getcwd)
CWD="$(pwd)"
echo "(allow file-read* (subpath \"$CWD\"))" >> "$SANDBOX_PROFILE"

echo "============================================"
echo "Testing standalone app (sandbox-exec mode)"
echo "============================================"
echo "App: $APP_PATH"
if [ -n "$APP_BUNDLE" ]; then
    echo "Bundle: $APP_BUNDLE"
fi
echo ""
echo "Sandbox restrictions:"
if [ "$ALLOW_NETWORK" = true ]; then
    echo "  - Network: ALLOWED (--allow-network)"
else
    echo "  - Network: DENIED"
fi
if [ "$ALLOW_SDK" = true ]; then
    echo "  - SDK (Xcode/CLT): ALLOWED"
else
    echo "  - SDK (Xcode/CLT): DENIED (--no-sdk)"
fi
echo "  - /opt/homebrew: DENIED"
echo "  - /usr/local: DENIED"
echo "  - ~/.jank, ~/.clojure, ~/.m2: DENIED"
echo "  - System libraries: allowed"
echo "  - App bundle: allowed"
echo ""
echo "Starting app in sandbox..."
echo "============================================"
echo ""

# Run the app with sandbox-exec and minimal environment
env -i \
    HOME="$HOME" \
    USER="$USER" \
    TMPDIR="$TMPDIR" \
    PATH="/usr/bin:/bin:/usr/sbin:/sbin" \
    TERM="$TERM" \
    /usr/bin/sandbox-exec -f "$SANDBOX_PROFILE" "$APP_PATH" "$@"
