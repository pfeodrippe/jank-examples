# iOS JIT Fixes Applied - 2026-01-02

## Summary

Two critical bugs in jank were identified and fixed that prevented iOS JIT from loading modules properly.

## Bugs Fixed

### Bug 1: binding_scope Push/Pop Asymmetry

**Location**: `compiler+runtime/src/cpp/jank/runtime/context.cpp`

**Root Cause**: The no-argument `binding_scope` constructor would call `push_thread_bindings()` which returns early WITHOUT pushing anything if the binding stack is empty. But the destructor ALWAYS tries to pop, creating an asymmetry.

**Why JIT Only**: In AOT mode, `clojure.core` loads first and sets up the binding stack before user modules. In JIT mode, `clojure.core` was already AOT-compiled on the dev machine, so its entry function doesn't run on iOS, leaving the stack empty.

**Fix Applied**:
- Added `bool pushed{ false }` member to `binding_scope` struct in `context.hpp`
- Modified constructor to check if stack is empty before calling push
- Only sets `pushed = true` when actually pushing
- Destructor only pops if `pushed` is true

### Bug 2: ns-publics Includes Private Vars

**Location**: `compiler+runtime/src/jank/clojure/core.jank:4196`

**Root Cause**: The `ns-publics` function had a TODO comment "Check for visibility" but never actually filtered out private vars. When `(refer clojure.core)` was called, it would try to refer private vars like `sleep`, creating malformed libspecs like `[sleep #'clojure.core/sleep]`.

**Error Message**: `not a libspec: [sleep #'clojure.core/sleep]`

**Fix Applied**:
```clojure
;; Changed from:
(if (= ns (cpp/clojure.core_native.var_ns v))
  (assoc acc k v)
  acc)

;; To:
(if (and (= ns (cpp/clojure.core_native.var_ns v))
         (not (:private (meta v))))
  (assoc acc k v)
  acc)
```

## Results After Fixes

The iOS JIT app now:
- Loads all modules successfully
- Initializes properly ("Mesh loaded successfully!", "Objects loaded: 6")
- Runs the main loop with proper camera sync (float values, not nil)
- Auto-rotate works (angle-y increments properly)

## Remaining Issue

After running for a while, the app crashes with:
```
[EXPECT_OBJECT] null ref for type integer
[jank] Error calling -main (std): [EXPECT_OBJECT] null ref when expecting type integer
```

This is a separate issue - something is returning nil where an integer is expected. The camera sync works correctly, so this is likely in another part of the draw loop (poll-events!, sync-edit-mode-from-cpp!, or elsewhere).

## Commands Used

```bash
# Build jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Test iOS JIT
cd /Users/pfeodrippe/dev/something
make ios-jit-clean && make ios-jit-sim-run 2>&1 | tee /tmp/ios-jit-test.txt
```

## Files Modified

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/context.hpp`
   - Added `bool pushed{ false }` to binding_scope struct

2. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/context.cpp`
   - Modified binding_scope constructors and destructor

3. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/jank/clojure/core.jank`
   - Fixed ns-publics to filter private vars

## Investigation Files Created

- `ai/20260102-ios-jit-not-a-number-nil-investigation.md` - Detailed analysis plan
