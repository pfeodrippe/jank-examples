# CI Fixes: jank Clang Compatibility

## Date: 2025-12-13

## Problem
The CI was failing during `make test` with error:
```
/Users/runner/jank/compiler+runtime/include/cpp/jtl/memory.hpp:24:5: error: static assertion failed: declval not allowed in an evaluated context
```

## Root Cause
The `run_tests.sh` script was:
1. Using `/usr/bin/clang++` (AppleClang 15) instead of jank's built clang (version 22)
2. Using hardcoded paths like `/Users/pfeodrippe/dev/jank/compiler+runtime`

jank's headers (specifically `jtl/memory.hpp`) use C++23 features like `static_assert(false, ...)` in templates which only trigger when instantiated. AppleClang 15 doesn't handle this correctly and evaluates the static_assert even when the template isn't instantiated.

## Solution
Updated `bin/run_tests.sh` to:
1. Dynamically detect jank source directory (supports both local dev and CI)
2. Use jank's built clang (`$JANK_SRC/build/llvm-install/usr/local/bin/clang++`)
3. Set `SOMETHING_DIR` to `$(pwd)` instead of hardcoded path

Also removed the now-unnecessary "Update project paths for CI" step from `.github/workflows/ci.yml`.

## Commands Run
```bash
# Test locally
rm -f vendor/vybe/vybe_flecs_jank.o
bash bin/run_tests.sh

# Check CI status
gh run list --workflow=ci.yml --limit=3
gh run view <run_id> --log-failed
```

## Key Lesson
When compiling C++ code that includes jank headers, always use jank's built clang (version 22+), not system clang. The jank headers rely on modern C++ features that require a recent clang version.

## Files Modified
- `bin/run_tests.sh` - Dynamic path detection and use jank's clang
- `.github/workflows/ci.yml` - Removed sed path replacement step
- `build_raylib.sh` - New script to build raylib_jank library
- `Makefile` - Added `build-raylib` target to `build-deps`

## Second Issue: Missing raylib_jank library

After fixing the clang issue, CI failed with:
```
error: Failed to load dynamic library 'raylib_jank': dlopen(raylib_jank, 0x0009): tried: 'raylib_jank' (no such file)
```

The `libraylib_jank.a` was present locally but not tracked in git. Created `build_raylib.sh` to:
1. Extract object files from `libraylib.a` (if needed)
2. Compile `raylib_jank_wrapper.cpp`
3. Create `libraylib_jank.a` static library
4. Create `libraylib_jank.dylib` dynamic library (macOS)

## Next Steps
- Wait for CI to pass after user commits changes
- If successful, can proceed with Linux pipeline work
