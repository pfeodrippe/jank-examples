# iOS JIT Stale PCH Symbol Resolution Fix

**Date**: 2026-01-03
**Issue**: iOS JIT compile server failing with `Symbols not found: [ __ZN4jank7runtime6detail12jank_nil_ptrE ]`

## Problem Description

The iOS JIT compile server was failing when trying to compile user namespaces with error:
```
JIT session error: Symbols not found: [ __ZN4jank7runtime6detail12jank_nil_ptrE ]
error: Failed to materialize symbols: { (main, { __ZN4vybe3sdf4math11to_double_1D2Ev, ... }) }
```

The symbol `__ZN4jank7runtime6detail12jank_nil_ptrE` is the mangled C++ name for `jank::runtime::detail::jank_nil_ptr`.

## Root Cause

In a previous session, I attempted to fix a different issue by adding a static `jank_nil_ptr` pointer to avoid calling `jank_const_nil()` during static initialization. The changes were:

1. Added `namespace jank::runtime::detail { extern obj::nil *jank_nil_ptr; }` to `oref.hpp`
2. Added definition in `nil.cpp`
3. Changed default member initializers to use `detail::jank_nil_ptr` instead of `jank_const_nil()`

These changes caused static initialization order issues and were reverted. However, **the iOS JIT resources retained stale PCH files** that were built with the old headers.

## Investigation Steps

1. **Checked source headers** - Both jank and iOS resource headers were clean (no `jank_nil_ptr`)
2. **Checked PCH files for strings** - No `jank_nil_ptr` found in PCH binary content
3. **Found stale iOS JIT build directory** - `/Users/pfeodrippe/dev/jank/compiler+runtime/build-ios-sim-jit/` had object files newer than the jank binary
4. **Identified mismatch** - iOS resources had been synced with headers during the failed fix attempt

## Solution

Complete clean rebuild of all iOS JIT artifacts:

```bash
# 1. Clean all iOS JIT artifacts (now includes PCH and jank build dirs)
make ios-jit-clean

# 2. Rebuild iOS simulator runtime
make ios-sim-runtime

# 3. Rebuild iOS JIT app
make ios-jit-sim-build

# 4. Start compile server and app
make ios-compile-server-sim &
xcrun simctl launch --console-pty booted com.vybe.SdfViewerMobile-JIT-Sim
```

## Key Learnings

1. **PCH files can contain stale symbol references** - Even if source headers are clean, cached PCH files from a previous build can reference symbols that no longer exist

2. **iOS JIT has multiple build layers to clean** (all now handled by `make ios-jit-clean`):
   - iOS resources PCH: `SdfViewerMobile/jank-resources/incremental.pch`
   - iOS app build: `SdfViewerMobile/build-iphonesimulator-jit/`
   - Jank iOS JIT lib: `$(JANK_DIR)/build-ios-sim-jit/`
   - Xcode DerivedData: `~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-*`

3. **Always clean when reverting header changes** - If you modify headers and then revert, you must rebuild ALL dependent PCH files and object files

4. **Makefile was updated** - Added missing clean targets to `ios-jit-clean` for jank build dirs and PCH files

## Result

After complete clean rebuild:
- iOS JIT app launches successfully
- All 9 user modules load correctly
- No symbol lookup failures
- App runs at ~90-113 FPS
- Greeting string displays correctly without corruption
