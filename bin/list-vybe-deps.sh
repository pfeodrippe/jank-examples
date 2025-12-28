#!/usr/bin/env bash
# List and optionally evaluate vybe modules transitively required by a given module

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
JANK_SRC_DIR="$SCRIPT_DIR/../SdfViewerMobile/jank-resources/src/jank"
MODULE="$1"
NREPL_PORT="$2"
VISITED_FILE=$(mktemp)

if [ -z "$MODULE" ]; then
    echo "Usage: $0 <module-name> [nrepl-port]"
    echo "  If nrepl-port is provided, evaluates each file via nREPL"
    exit 1
fi

trap "rm -f $VISITED_FILE" EXIT

# Convert module name to file path
module_to_path() {
    echo "$JANK_SRC_DIR/$(echo "$1" | tr '.' '/')".jank
}

# Check if module was visited
is_visited() {
    grep -qx "$1" "$VISITED_FILE" 2>/dev/null
}

# Mark module as visited
mark_visited() {
    echo "$1" >> "$VISITED_FILE"
}

# Extract required modules from a jank file
get_requires() {
    local file="$1"
    if [ -f "$file" ]; then
        grep -oE '\[vybe\.[a-z._-]+' "$file" | sed 's/\[//' | sort -u
    fi
}

# Recursively collect all vybe dependencies (dependencies first)
collect_deps() {
    local mod="$1"

    # Skip if already visited or not a vybe module
    if is_visited "$mod"; then
        return
    fi

    case "$mod" in
        vybe.*) ;;
        *) return ;;
    esac

    mark_visited "$mod"

    # Process dependencies first (depth-first)
    local file=$(module_to_path "$mod")
    for dep in $(get_requires "$file"); do
        collect_deps "$dep"
    done

    # Then output this module (so dependencies come before dependents)
    echo "$mod"
}

# Collect modules in dependency order
MODULES=$(collect_deps "$MODULE")

if [ -z "$NREPL_PORT" ]; then
    # Just list modules
    echo "$MODULES"
else
    # Evaluate each module via nREPL
    echo "Evaluating vybe modules via nREPL on port $NREPL_PORT..."
    echo ""

    for mod in $MODULES; do
        file=$(module_to_path "$mod")
        if [ -f "$file" ]; then
            printf "%-30s " "$mod"
            # Read file and evaluate via nREPL
            result=$(clj-nrepl-eval -p "$NREPL_PORT" "$(cat "$file")" 2>&1)
            if echo "$result" | grep -q "^=>"; then
                echo "OK"
            else
                echo "FAILED"
                echo "$result"
            fi
        else
            echo "$mod: FILE NOT FOUND"
        fi
    done
fi
