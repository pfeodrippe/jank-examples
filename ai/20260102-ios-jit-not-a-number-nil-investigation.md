# iOS JIT "not a number: nil" Error - Deep Investigation Plan

## Date: 2026-01-02

## Current Error State
```
[jank] binding_scope pop failed: Mismatched thread binding pop
  current *ns*: clojure.core
  current *file*: "NO_SOURCE_PATH"
[jank] Error calling -main (std): not a number: nil
```

## Critical Discovery: Push/Pop Asymmetry Bug

### The Root Cause

In `context.cpp:1209-1224`, there's a fundamental asymmetry in the binding scope mechanism:

```cpp
jtl::string_result<void> context::push_thread_bindings()
{
  auto bindings(obj::persistent_hash_map::empty());
  auto &tbfs(thread_binding_frames[std::this_thread::get_id()]);
  if(!tbfs.empty())
  {
    bindings = tbfs.front().bindings;
  }
  /* Nothing to preserve, if there are no current bindings. */
  else
  {
    return ok();  // <-- RETURNS WITHOUT PUSHING!!!
  }

  return push_thread_bindings(bindings);
}
```

**The Bug**: When `push_thread_bindings()` (no-argument version) is called with an empty stack, it returns early WITHOUT actually pushing anything. But `binding_scope::~binding_scope()` ALWAYS calls `pop_thread_bindings()`.

This creates an asymmetric push/pop pair:
- `binding_scope` constructor -> calls `push_thread_bindings()` -> returns early if empty (NO PUSH)
- `binding_scope` destructor -> calls `pop_thread_bindings()` -> tries to pop -> FAILS (empty stack)

### Why This Works in AOT But Fails in JIT

**AOT Mode**:
1. `clojure.core` loads first during AOT compilation
2. Its entry function pushes initial bindings for `*ns*` and `*file*`
3. All subsequent modules run with non-empty binding stack
4. The no-arg `push_thread_bindings()` finds non-empty stack, so it actually pushes

**JIT Mode**:
1. iOS app starts, `clojure.core` is loaded from AOT (binding stack set up on compile server, not iOS)
2. JIT modules are loaded remotely - their entry functions run on iOS
3. First JIT module's entry function calls `push_thread_bindings()` with empty stack
4. Returns early without pushing
5. When the module finishes, destructor tries to pop
6. **CRASH**: Mismatched thread binding pop

### The Cascade Effect

1. First binding_scope fails to push (empty stack)
2. Destructor tries to pop -> fails with error
3. Runtime state is now inconsistent
4. `*ns*` shows as `clojure.core` because initial bindings were never properly set
5. Var lookups may fail or return nil
6. Later, `sync-camera-from-cpp!` or `state/get-camera` returns nil values
7. Math operation on nil -> "not a number: nil"

## Investigation Data Points

### 1. The Error Sequence
- Binding scope error happens FIRST
- Math error happens SECOND
- This supports the theory that corrupted binding state leads to nil values

### 2. Code Path Analysis

**sync-camera-from-cpp!** (in ios.jank):
```clojure
(defn sync-camera-from-cpp! []
  (let [d (sdfx/get_camera_distance)
        ax (sdfx/get_camera_angle_x)
        ay (sdfx/get_camera_angle_y)
        ty (sdfx/get_camera_target_y)]
    (state/set-camera!
     {:distance d :angle-x ax :angle-y ay :target-y ty})))
```

These C++ functions return floats. If they fail to resolve in JIT mode, they could return nil.

**state/set-camera!** (in state.jank):
```clojure
(defn set-camera! [camera-map]
  (reset! *camera* (merge @*camera* camera-map)))
```

If `*camera*` atom wasn't properly initialized due to defonce issues in JIT mode, `@*camera*` could be nil.

**The draw function** uses:
```clojure
(when (ui/auto-rotate?)
  (state/update-camera! update :angle-y + 0.01)  ;; Math here!
```

If `(:angle-y @*camera*)` is nil, then `(+ nil 0.01)` fails.

### 3. The defonce Issue

In JIT mode, `defonce` behavior may differ:
```clojure
(defonce *camera* (atom {:distance 8.0 ...}))
```

If:
- The var is already defined from a previous attempt
- But the atom wasn't properly initialized
- Then `@*camera*` returns nil

### 4. The u/v->p Issue

The UI uses:
```clojure
(defonce *auto-rotate (u/v->p false))
```

Which expands to:
```clojure
(defonce *auto-rotate (cpp/box (cpp/new (cpp/type "bool") false)))
```

If `cpp/new` or `cpp/box` fails in JIT mode, `*auto-rotate` could be nil or an invalid pointer.

## Fix Strategies

### Strategy 1: Fix the Push Asymmetry (RECOMMENDED)

**File**: `compiler+runtime/src/cpp/jank/runtime/context.cpp`

**Option A**: Track if push actually happened

```cpp
context::binding_scope::binding_scope()
  : pushed{ false }
{
  auto res = __rt_ctx->push_thread_bindings();
  if(res.is_ok())
  {
    pushed = true;
  }
}

context::binding_scope::~binding_scope()
{
  if(pushed)
  {
    __rt_ctx->pop_thread_bindings().expect_ok();
  }
}
```

**Option B**: Always push (even with empty bindings)

```cpp
jtl::string_result<void> context::push_thread_bindings()
{
  auto bindings(obj::persistent_hash_map::empty());
  auto &tbfs(thread_binding_frames[std::this_thread::get_id()]);
  if(!tbfs.empty())
  {
    bindings = tbfs.front().bindings;
  }
  // REMOVED: early return for empty case
  // Always push, even if bindings are empty
  return push_thread_bindings(bindings);
}
```

### Strategy 2: Initialize Binding Stack Before JIT Loading

Ensure the binding stack has initial values before any JIT modules are loaded:

```cpp
// In iOS JIT init code, before loading any modules:
__rt_ctx->push_thread_bindings(create_initial_bindings());
```

Where `create_initial_bindings()` creates a map with:
- `*ns*` -> clojure.core namespace
- `*file*` -> "NO_SOURCE_PATH"

### Strategy 3: Debug The nil Source First

Add debug output to identify exactly WHERE nil comes from:

```clojure
;; In sync-camera-from-cpp!:
(println "[DEBUG sync-camera] distance=" d "angle-x=" ax "angle-y=" ay "target-y=" ty)
```

This was already added but needs to run to see output.

## Implementation Plan

### Phase 1: Verify the Root Cause
1. Add debug output to `push_thread_bindings()` and `pop_thread_bindings()` in context.cpp
2. Build jank and run iOS JIT
3. Observe whether push returns early for empty stack

### Phase 2: Implement the Fix
1. Choose Strategy 1 Option A (track `pushed` flag) - safest change
2. Modify `context.hpp` to add `bool pushed` member to `binding_scope`
3. Modify `context.cpp` to set/check the flag
4. Build and test

### Phase 3: Verify Fix Resolves Both Errors
1. Run iOS JIT
2. Verify no more binding_scope pop failures
3. Verify no more "not a number: nil" errors
4. Verify camera values are properly initialized

## Commands

```bash
# Build jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Run iOS JIT
cd /Users/pfeodrippe/dev/something
make ios-jit-clean && make ios-jit-sim-run 2>&1 | tee /tmp/ios-jit-output.txt

# Check output
cat /tmp/ios-jit-output.txt | grep -E "(binding_scope|not a number|DEBUG)"
```

## Files Modified

1. **`compiler+runtime/include/cpp/jank/runtime/context.hpp`** - DONE
   - Added `bool pushed{ false }` member to `binding_scope` struct

2. **`compiler+runtime/src/cpp/jank/runtime/context.cpp`** - DONE
   - Modified no-arg `binding_scope` constructor to check if stack is empty first
   - Only sets `pushed = true` when actually pushing
   - Modified destructor to only pop if `pushed` is true

### Implementation Details

```cpp
// context.hpp - Added member:
struct binding_scope
{
  binding_scope();
  binding_scope(obj::persistent_hash_map_ref const bindings);
  ~binding_scope();

  /* Track whether we actually pushed, to avoid asymmetric pop when stack was empty */
  bool pushed{ false };
};

// context.cpp - Modified constructor:
context::binding_scope::binding_scope()
{
  /* The no-arg push_thread_bindings() returns ok() without pushing if the stack is empty.
   * We need to check if the stack was empty before calling to know if we'll need to pop. */
  auto const &tbfs(__rt_ctx->thread_binding_frames[std::this_thread::get_id()]);
  if(!tbfs.empty())
  {
    __rt_ctx->push_thread_bindings().expect_ok();
    pushed = true;
  }
  /* If stack was empty, we don't push and don't need to pop later */
}

// context.cpp - Modified destructor:
context::binding_scope::~binding_scope()
{
  /* Only pop if we actually pushed */
  if(!pushed)
  {
    return;
  }
  // ... rest of pop logic
}
```

## Related Analysis

This bug was likely dormant because:
1. In normal jank usage, clojure.core always loads first with proper bindings
2. The binding stack is never empty when user code runs
3. JIT iOS loading is a special case where modules run on a fresh thread without prior bindings

## Summary

### Bug 1: Asymmetric Push/Pop in binding_scope
**Root Cause**: Asymmetric push/pop in `binding_scope` when stack is empty
**Effect**: Corrupted binding state leads to nil values in runtime
**Fix**: Track whether push actually happened, only pop if it did
**Files Modified**: `context.hpp`, `context.cpp`
**Status**: FIXED

### Bug 2: ns-publics Includes Private Vars
**Root Cause**: `ns-publics` function has TODO: "Check for visibility" but never actually checks
**Effect**: `(refer clojure.core)` tries to refer `sleep` (private), creates malformed libspec `[sleep #'clojure.core/sleep]`
**Fix**: Added `(not (:private (meta v)))` check in `ns-publics`
**File Modified**: `clojure/core.jank` line 4206-4207
**Status**: FIXED

**Complexity**: Low - both fixes are small, localized changes
**Risk**: Low - fixes are defensive and backward compatible
