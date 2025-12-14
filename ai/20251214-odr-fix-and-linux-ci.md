# ODR Fix for Standalone Builds + CI Caching Improvements

## Date: 2025-12-14

## Problem: Standalone App Crash (SIGABRT in ImGui)

CI-downloaded `.app` crashed with:
```
Assertion failed: (io.BackendPlatformUserData != __null && "Context or backend not initialized!
Did you call ImGui_ImplSDL3_Init()? Did you call ImGui_ImplSDL3_Shutdown()?")
```

### Root Cause: ODR (One Definition Rule) Violation

The `_GImGui` global static symbol was present in **two places**:
1. `libsdf_deps.dylib` (built from OBJ_FILES)
2. Main executable (via `--obj` flags passed to jank)

This caused two separate ImGui context instances - one initialized, one not.

### Fix Applied

Removed duplicate `--obj` flags from standalone build section in `bin/run_sdf.sh`:

```bash
# BEFORE (buggy):
for obj in "${OBJ_FILES[@]}"; do
    JANK_ARGS+=(--obj "$obj")
done

# AFTER (fixed):
# NOTE: Don't pass object files via --obj for standalone builds!
# They're already in libsdf_deps.dylib. Including them twice causes
# ODR violations (duplicate ImGui global context _GImGui).
# The dylib is passed via --jit-lib above for JIT symbol resolution.
```

### Why JIT Mode is Different

JIT mode still uses `--obj` flags because:
- No dylib is created
- Object files are loaded directly by the JIT linker
- Only one copy of each symbol exists

## Linux CI Status

Linux CI fails during **jank compiler build** (not this project):
```
/usr/include/c++/14/any:371:14: error: '_Manager_internal' redeclared with 'public' access
```

This is a GCC 14 / Clang 22 incompatibility issue with Boost.Asio's use of `<any>`.
Needs to be fixed upstream in the jank repository.

## macOS CI Status: PASSED

Both jobs completed successfully after the ODR fix.

## Files Modified
- `bin/run_sdf.sh` - Removed duplicate `--obj` flags in standalone section
- `vulkan/sdf_engine.hpp` - Added `e->initialized` guards (earlier fix)

## Commands Used
```bash
# Check CI status
gh run view <run_id> --json jobs --jq '.jobs[] | "\(.name): \(.status) \(.conclusion // "running")"'

# Check for duplicate symbols
nm SDFViewer.app/Contents/MacOS/SDFViewer-bin 2>/dev/null | grep "_GImGui"
nm SDFViewer.app/Contents/Frameworks/libsdf_deps.dylib 2>/dev/null | grep "_GImGui"

# View CI failure logs
gh run view <run_id> --log-failed
```

## CI Caching Improvements

### Problem
- jank takes ~10-15 min to build (on top of ~90 min for Clang/LLVM)
- Every CI run was rebuilding jank even with same commit

### Solution
Added commit-based caching for the full jank build:

1. **New job `get-jank-commit`**: Fetches the latest commit hash from jank repo
2. **Cache key**: `jank-{os}-{arch}-{commit_hash}`
3. **Full build directory cached**: `~/jank/compiler+runtime/build` (includes both LLVM and jank)

### CI Structure (Before)
```
build-clang-macos  →  build-macos
build-clang-linux  →  build-linux
```

### CI Structure (After)
```
get-jank-commit  →  build-jank-macos  →  build-macos
                 →  build-jank-linux  →  build-linux
```

### Benefits
- **Cache hit**: Skip ~100+ min of builds (Clang + jank)
- **Cache miss**: Still builds everything, then caches for next run
- **Deterministic**: Same jank commit = same cache key

### Cache Key Example
```yaml
key: jank-macos-arm64-97fed5973007ad4ef16e43e3635208ce3d4569ad
```

## Key Lessons

1. **ODR violations in macOS are sneaky** - Two-level namespace means different load order can produce different behavior (local vs CI builds)
2. **Check for duplicate symbols** when linking both object files AND dylibs containing the same code
3. **JIT vs AOT linking differ** - JIT loads symbols directly, AOT links against libraries
4. **Cache entire build directories** - Caching just intermediate artifacts means rebuilding final outputs
