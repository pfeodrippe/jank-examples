# CI Fix: $HOME Variable Expansion Issue

## Date: 2026-01-02

## Summary

Fixed GitHub Actions CI failures where `$HOME` variable was not being expanded correctly in make commands, causing builds to fail with "No such file or directory" errors.

## Issue

The CI was failing with errors like:
```
make: *** No rule to make target `/jank/compiler+runtime/include/cpp/jank/type.hpp', needed by `vendor/vybe/vybe_flecs_jank.o'.  Stop.
```

Notice the path is `/jank/...` instead of `/Users/runner/jank/...` or `$HOME/jank/...`.

### Affected Jobs
- Build and Test - macOS (step: "Build standalone app")
- Build iOS JIT Simulator (step: "Build iOS JIT Simulator")
- Build iOS AOT Simulator (step: "Build iOS AOT Simulator")

## Root Cause

In GitHub Actions workflow (`.github/workflows/ci.yml`), make commands were using unquoted `$HOME` variable:

```yaml
make sdf-standalone JANK_SRC=$HOME/jank/compiler+runtime
```

When `$HOME` is unquoted in shell contexts, it can fail to expand correctly, resulting in:
- `JANK_SRC=/jank/compiler+runtime` (incorrect - $HOME expanded to empty)
- Instead of: `JANK_SRC=/Users/runner/jank/compiler+runtime` (correct)

This caused Make to look for jank headers in the wrong location.

## Fix

Added quotes around all `JANK_SRC` parameter assignments in the workflow:

```yaml
# Before (incorrect)
make sdf-standalone JANK_SRC=$HOME/jank/compiler+runtime

# After (correct)
make sdf-standalone JANK_SRC="$HOME/jank/compiler+runtime"
```

### Changed Lines in `.github/workflows/ci.yml`
1. Line 165: `make test` for macOS
2. Line 171: `make sdf-standalone` for macOS
3. Line 255: `make ios-aot-sim-build` for iOS AOT Simulator
4. Line 369: `make ios-jit-sim-build` for iOS JIT Simulator
5. Line 583: `make test` for Linux (disabled but fixed for future)
6. Line 592: `make sdf-standalone` for Linux (disabled but fixed for future)

## Testing

Tested locally with:
```bash
cd ~/dev/something
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export PATH="$HOME/jank/compiler+runtime/build:$PATH"
make sdf-standalone JANK_SRC="$HOME/jank/compiler+runtime"
```

Without quotes, the command fails with the same error seen in CI. With quotes, it works correctly.

## Context

This issue appeared after commit `3457055 "Try to fix CI"` which removed bpptree references. The bpptree removal was correct (it didn't exist in the jank repo), but the CI was still failing due to this separate quoting issue that existed in the workflow all along.

## Related Issues

- Previous commit (`3457055`) fixed the bpptree issue documented in `20260102-ios-jit-ci-fix-and-immer-abi.md`
- This fix addresses the remaining CI failures in the same GitHub Actions run

## Files Changed

1. `.github/workflows/ci.yml` - Added quotes to 6 JANK_SRC parameter assignments
