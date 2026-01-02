# CI Fix: Missing jank-phase-1 Binary in Cache

**Date**: 2026-01-01
**CI Run**: https://github.com/pfeodrippe/jank-examples/actions/runs/20649623155/job/59293174850
**Status**: FAILED
**Error**: `/Users/runner/jank/compiler+runtime/build/jank: No such file or directory`

## Problem

After fixing the phase 2 enablement logic (commit e076944 + jank CMakeLists.txt fix), the CI is now **building successfully** but **verify step fails** because the jank binary doesn't exist when the cache is restored.

## Root Cause

### The Symlink Issue

When phase 2 is **disabled** (first build, no core.cpp exists), the build creates:
1. **`jank-phase-1`** - The actual binary
2. **`jank`** - A symlink pointing to `jank-phase-1`

From `CMakeLists.txt:1493-1500`:
```cmake
else()  # Phase 2 disabled
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/jank
    COMMAND ln -sf jank-phase-1 jank  # <-- Creates symlink!
  )
```

### What Was Cached (v4)

Cache configuration in commit e076944:
```yaml
path: |
  ~/jank/compiler+runtime/build/jank              # ✅ Cached (symlink)
  ~/jank/compiler+runtime/build/core-libs         # ✅ Cached
  # ❌ Missing: jank-phase-1 (the actual binary!)
```

### What Happened

1. **build-jank-macos job** (cache miss):
   - Builds `jank-phase-1` binary
   - Creates symlink `jank -> jank-phase-1`
   - Caches the **symlink** but NOT the actual `jank-phase-1` binary

2. **build-macos job** (cache hit):
   - Restores cache
   - Gets `jank` symlink
   - But `jank-phase-1` doesn't exist!
   - Symlink points to nothing
   - `~/jank/compiler+runtime/build/jank check-health` fails

## Solution

Add **both** the symlink and the actual binary to cache, plus the `classes/` directory with AOT-compiled `.o` files:

```yaml
path: |
  ~/jank/compiler+runtime/build/jank              # Symlink (or phase 2 binary)
  ~/jank/compiler+runtime/build/jank-phase-1      # Phase 1 binary (actual file)
  ~/jank/compiler+runtime/build/classes           # AOT-compiled .o files
  ~/jank/compiler+runtime/build/core-libs         # Generated C++ sources
  # ... rest of cache paths
```

## Why This Wasn't Caught Earlier

The issue only manifests when:
1. Phase 2 is disabled (which happens on first build with the new fix)
2. Cache is restored in a different job
3. The restored symlink points to a non-existent file

With the old broken config, the build never succeeded, so we never got to the cache restore phase in dependent jobs.

## Files Modified

**In ~/dev/something/.github/workflows/ci.yml**:

### Change 1: Added to cache paths
- `~/jank/compiler+runtime/build/jank-phase-1`
- `~/jank/compiler+runtime/build/classes`

### Change 2: Bumped cache versions
- macOS: `v4` → `v5`
- Linux: `v13` → `v14`

Applied to 6 locations (same as before):
- build-jank-macos: Cache jank build (lines 76-90)
- build-macos: Restore jank build cache (lines 135-149)
- build-ios-aot: Restore jank build cache (lines 205-219)
- build-ios-jit: Restore jank build cache (lines 277-291)
- build-jank-linux: Cache jank build (disabled, lines 402-422)
- build-linux: Restore jank build cache (disabled, lines 530-550)

## What Gets Cached Now (v5)

Complete cache contents:
```
~/jank/compiler+runtime/build/
├── jank                  # Symlink to jank-phase-1 (or phase 2 binary)
├── jank-phase-1          # Phase 1 binary (always exists)
├── CMakeCache.txt        # CMake configuration
├── CMakeFiles/           # CMake build state
├── *.a                   # Static libraries
├── *.ninja               # Ninja build files
├── lib/                  # Runtime libraries
├── core-libs/            # Generated C++ sources (core.cpp, etc.)
│   └── clojure/core.cpp
└── classes/              # AOT-compiled object files
    ├── clojure/core.o
    ├── jank/nrepl_server/core.o
    ├── jank/nrepl_server/server.o
    └── jank/export.o
```

## Testing Plan

1. Push changes to develop branch
2. First build (cache miss):
   - Should build jank-phase-1
   - Should create jank symlink
   - Should cache both files
3. Subsequent jobs (cache hit):
   - Should restore both jank and jank-phase-1
   - `jank check-health` should succeed
   - Tests should run

## Summary

**Fix 1** (Previous): Check if core.cpp exists before enabling phase 2 ✅
**Fix 2** (Previous): Add core-libs to cache ✅
**Fix 3** (This fix): Add jank-phase-1 and classes to cache ✅

All three fixes are required for CI to work correctly!
