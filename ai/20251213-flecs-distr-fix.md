# CI Fix: Remove Unused Flecs Wrapper + Fix Hardcoded Paths

## Date: 2025-12-13

## Problem 1: Missing flecs_jank_wrapper_native.o
CI was failing with:
```
Uncaught exception: failed to load object file: vendor/flecs/distr/flecs_jank_wrapper_native.o
```

### Root Cause
- `flecs_jank_wrapper.cpp` was a local custom wrapper providing `jank_flecs_*` functions
- **But nobody uses these wrapper functions!** The code uses direct header includes instead

### Solution
Removed `flecs_jank_wrapper_native.o` from all scripts - it was dead code.

## Problem 2: Hardcoded jank paths in run_sdf.sh
CI failed with:
```
cp: /Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/lib/../bin/clang-22: No such file or directory
```

### Root Cause
`run_sdf.sh` had hardcoded paths like `/Users/pfeodrippe/dev/jank/...` instead of detecting the jank location.

### Solution
Added path detection at the top of `run_sdf.sh`:
```bash
if [ -d "/Users/pfeodrippe/dev/jank/compiler+runtime" ]; then
    JANK_SRC="/Users/pfeodrippe/dev/jank/compiler+runtime"
elif [ -d "$HOME/jank/compiler+runtime" ]; then
    JANK_SRC="$HOME/jank/compiler+runtime"
fi
```

## Files Modified
- `bin/run_tests.sh` - Removed unused wrapper
- `bin/run_sdf.sh` - Removed unused wrapper + dynamic jank path detection
- `bin/run_integrated.sh` - Removed unused wrapper

## CI Status: ✅ PASSED
Both jobs completed successfully:
- Build Clang - macOS: success (cached)
- Build and Test - macOS: success

## Key Lessons
1. Before tracking/building a file, check if it's actually used
2. Use path detection logic to support both local dev and CI environments
3. The flecs submodule already has `distr/flecs.c` and `distr/flecs.h` - no need to track separately
