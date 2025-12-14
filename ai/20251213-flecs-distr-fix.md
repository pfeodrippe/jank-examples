# CI Fix: Remove Unused Flecs Wrapper

## Date: 2025-12-13

## Problem
CI was failing with:
```
Uncaught exception: failed to load object file: vendor/flecs/distr/flecs_jank_wrapper_native.o
```

## Root Cause
- `vendor/flecs` is a git submodule with `distr/flecs.c` and `distr/flecs.h` (part of official repo)
- `flecs_jank_wrapper.cpp` was a local custom wrapper providing `jank_flecs_*` functions
- **But nobody uses these wrapper functions!** The code uses direct header includes instead:
  ```clojure
  ["flecs.h" :as fl :scope ""]
  ```

## Solution
Simply remove the unused wrapper from the build:

1. Removed `flecs_jank_wrapper_native.o` from `run_tests.sh` OBJ_ARGS
2. The flecs submodule provides `flecs.c` and `flecs.h` - that's all we need

## Files Modified
- `bin/run_tests.sh` - Removed unused `flecs_jank_wrapper_native.o` from OBJ_ARGS

## Key Lesson
Before tracking/building a file, check if it's actually used! The `jank_flecs_*` wrapper functions had zero callers.

## Commands to Commit
```bash
git add bin/run_tests.sh
git commit -m "Fix CI: Remove unused flecs_jank_wrapper_native.o"
git push
```
