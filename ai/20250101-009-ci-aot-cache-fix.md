# CI Failure Analysis: jank-examples Missing AOT Core Libraries

**Date**: 2026-01-01
**CI Run**: https://github.com/pfeodrippe/jank-examples/actions/runs/20649463715/job/59291704538
**Status**: FAILED
**Error**: CMake cannot find `build/core-libs/clojure/core.cpp`

## Problem Statement

The jank-examples CI build is failing on macOS with the following error:

```
CMake Error at CMakeLists.txt:1450 (add_executable):
  Cannot find source file:
    /Users/runner/jank/compiler+runtime/build/core-libs/clojure/core.cpp

CMake Error at CMakeLists.txt:1450 (add_executable):
  No SOURCES given to target: jank_exe_phase_2
```

## Root Cause Analysis

### Understanding jank's Two-Phase Build

jank uses a two-phase build process:

1. **Phase 1**: Build the jank compiler and use it to AOT (Ahead-of-Time) compile the core Clojure libraries, generating C++ files like `build/core-libs/clojure/core.cpp`

2. **Phase 2**: Build jank again, but this time linking against the AOT-compiled core libraries for better performance

From `CMakeLists.txt:271`:
```cmake
set(jank_clojure_core_output "${CMAKE_BINARY_DIR}/core-libs/clojure/core.cpp")
```

From `CMakeLists.txt:1450-1451`:
```cmake
add_executable(
  jank_exe_phase_2
  ${jank_clojure_core_output}  # <- This file doesn't exist!
  ...
)
```

### The Caching Problem

Looking at `.github/workflows/ci.yml:76-87` in jank-examples:

```yaml
- name: Cache jank build
  id: cache-jank
  uses: actions/cache@v4
  with:
    path: |
      ~/jank/compiler+runtime/build/jank
      ~/jank/compiler+runtime/build/CMakeCache.txt
      ~/jank/compiler+runtime/build/CMakeFiles
      ~/jank/compiler+runtime/build/*.a
      ~/jank/compiler+runtime/build/*.ninja
      ~/jank/compiler+runtime/build/lib
    key: jank-build-macos-arm64-${{ needs.get-jank-commit.outputs.commit }}-v3
```

**The cache includes**:
- The jank executable
- CMake cache files
- Build artifacts (*.a, *.ninja)
- The lib directory

**The cache is MISSING**:
- `~/jank/compiler+runtime/build/core-libs/` - **This is the critical missing piece!**

When the cache is restored in subsequent jobs (`build-macos`, `build-ios-aot`, `build-ios-jit`), the jank binary exists, but the AOT-compiled core libraries are missing. When CMake tries to configure, it reads `CMakeCache.txt` which indicates that phase 2 is enabled, but then fails because `core-libs/clojure/core.cpp` doesn't exist.

### Why This Happened

The cache configuration was likely created before the two-phase build was implemented or the importance of caching the AOT output wasn't recognized. The `./bin/compile` script successfully builds everything when the cache misses (line 98 in the workflow), but the cache doesn't preserve the AOT-compiled libraries.

## Solution

Add `~/jank/compiler+runtime/build/core-libs` to all cache path configurations:

1. In `build-jank-macos` job (lines 76-87)
2. In all cache restore steps that reference this cache:
   - `build-macos` job (lines 135-145)
   - `build-ios-aot` job (lines 205-215)
   - `build-ios-jit` job (lines 277-287)

## Implementation

The fix requires updating the jank-examples repository's `.github/workflows/ci.yml` file to include the `core-libs` directory in the cache paths.

### Changes Required

**Before**:
```yaml
path: |
  ~/jank/compiler+runtime/build/jank
  ~/jank/compiler+runtime/build/CMakeCache.txt
  ~/jank/compiler+runtime/build/CMakeFiles
  ~/jank/compiler+runtime/build/*.a
  ~/jank/compiler+runtime/build/*.ninja
  ~/jank/compiler+runtime/build/lib
```

**After**:
```yaml
path: |
  ~/jank/compiler+runtime/build/jank
  ~/jank/compiler+runtime/build/CMakeCache.txt
  ~/jank/compiler+runtime/build/CMakeFiles
  ~/jank/compiler+runtime/build/*.a
  ~/jank/compiler+runtime/build/*.ninja
  ~/jank/compiler+runtime/build/lib
  ~/jank/compiler+runtime/build/core-libs
```

### Files to Modify

In the jank-examples repository (develop branch):
- `.github/workflows/ci.yml` - Update cache paths in 4 locations (lines 76-87, 135-145, 205-215, 277-287)

## Testing Plan

1. Apply the fix to the workflow file
2. Increment the cache version key from `v3` to `v4` to force a fresh cache build
3. Push to the develop branch
4. Verify the CI build succeeds
5. Check that subsequent builds use the cached core-libs successfully

## Notes

- The cache key uses the jank commit hash, so different jank commits will have different caches (correct behavior)
- The `JANK_SKIP_AOT_CHECK: 1` environment variable is unrelated to this issue - it skips runtime AOT checks in the check-health command
- The Linux build is currently disabled (`if: false`) due to unrelated TLS issues
- This same issue would affect the Linux build when it's re-enabled, so the fix should be applied there too (lines 405-415, 533-544)

## Impact

- **Severity**: High - CI completely broken
- **Scope**: All macOS and iOS builds in jank-examples
- **Fix Complexity**: Low - Single line addition to cache paths
- **Risk**: Low - Adding to cache paths is safe and backwards compatible

---

## UPDATE: Actual Root Cause (After commit e076944)

**CI Run**: https://github.com/pfeodrippe/jank-examples/actions/runs/20649623155
**Status**: STILL FAILING after cache fix!

The cache fix (commit e076944) was **necessary but NOT sufficient**. The CI is still failing because:

### The Real Problem

1. Workflow runs: `./bin/configure -DCMAKE_BUILD_TYPE=Release`
2. CMakeLists.txt:265 automatically enables phase 2 for Release builds:
   ```cmake
   if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR jank_force_phase_2)
     set(jank_enable_phase_2 ON)
   ```
3. CMake tries to add `${CMAKE_BINARY_DIR}/core-libs/clojure/core.cpp` to `jank_exe_phase_2`
4. **File doesn't exist yet** - it's generated during the first build!
5. CMake **configuration fails** before compilation even starts

### Why the Cache Fix Wasn't Enough

- Cache miss = no core-libs exist
- CMake configure runs BEFORE compilation
- Phase 2 is enabled by default for Release builds
- CMake fails trying to add a non-existent source file
- Build never reaches the compilation step that would generate core-libs

### The Complete Solution

**Fix 1**: Cache core-libs directory ✅ (Done in e076944)

**Fix 2**: Check if core.cpp exists before enabling phase 2 (Required!)

Modify `jank/compiler+runtime/CMakeLists.txt` around line 265:

```cmake
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR jank_force_phase_2)
  # Only enable phase 2 if core-libs already exist (from previous build or cache)
  set(core_cpp_path "${CMAKE_BINARY_DIR}/core-libs/clojure/core.cpp")
  if(EXISTS "${core_cpp_path}")
    set(jank_enable_phase_2 ON)
    message(STATUS "Found ${core_cpp_path}, enabling phase 2")
  else()
    set(jank_enable_phase_2 OFF)
    message(STATUS "core-libs/clojure/core.cpp not found, disabling phase 2 (will be built in phase 1)")
  endif()
else()
  set(jank_enable_phase_2 OFF)
endif()
```

This allows:
1. First build: Phase 2 disabled → generates core-libs
2. Subsequent builds (or cache hits): Phase 2 enabled → uses cached core-libs
