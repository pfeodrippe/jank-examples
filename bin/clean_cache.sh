#!/bin/bash
# Clean jank module cache to fix stale compilation issues
# Usage: ./bin/clean_cache.sh
#   or source this in other scripts: source "$(dirname "$0")/clean_cache.sh"

cd "$(dirname "$0")/.."

if [ -d "target" ]; then
    echo "Cleaning jank module cache (target/)..."
    rm -rf target/
    echo "Cache cleared."
else
    echo "No cache to clean."
fi
