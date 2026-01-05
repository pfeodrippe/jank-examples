#!/bin/bash
# Wrapper script for xcodegen that sets default environment variables
# Usage: ./generate-project.sh [spec-file]
#   spec-file: Optional, defaults to project-jit-sim.yml

set -e

# Set defaults for environment variables used in project specs
export JANK_SRC="${JANK_SRC:-/Users/pfeodrippe/dev/jank/compiler+runtime}"
export HOME="${HOME:-$HOME}"

SPEC_FILE="${1:-project-jit-sim.yml}"

echo "Generating Xcode project from $SPEC_FILE"
echo "  JANK_SRC=$JANK_SRC"

xcodegen generate --spec "$SPEC_FILE"
