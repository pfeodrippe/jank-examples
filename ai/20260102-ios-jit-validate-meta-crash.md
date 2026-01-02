# iOS JIT validate_meta Crash Investigation

## Problem Summary

After fixing the compile server issues (module redefinition, var initialization bypass, core modules skipping), the iOS JIT app now crashes with a different error during module loading:

**Crash Site:** `validate_meta` -> `to_string` -> `panic`

**Exception:** `EXC_CRASH (SIGABRT)` - panic in `to_string` when trying to format an object with invalid/corrupted type

## Stack Trace Analysis

```
7   var::with_meta + 24
6   validate_meta + 88
5   to_string + 4196  <- panic here
4   panic
...
10  load_jank + 3608  <- module loading
```

## Root Cause

The `validate_meta` function checks if metadata is a map:
```cpp
object_ref validate_meta(object_ref const m) {
  if(!is_map(m) && m.is_some()) {
    throw std::runtime_error{ util::format("invalid meta: {}", runtime::to_string(m)) };
  }
  return m;
}
```

The crash happens because:
1. `m.is_some()` is true (the metadata is not nil)
2. `is_map(m)` is false (the metadata is not a valid map)
3. `to_string(m)` panics because `m` has an invalid object type

This indicates **uninitialized or corrupted memory** - the metadata object has garbage in its type field.

## Likely Cause: Constant Initialization Order

The JIT-compiled code has a specific initialization order:
```cpp
extern "C" void jank_load_XXX() {
  // 1. Intern namespace
  jank_ns_intern_c("...");

  // 2. Initialize vars with placement new
  new (&ns::var1) jank::runtime::var_ref(...);

  // 3. Initialize constants with placement new
  new (&ns::const1) jank::runtime::obj::...;

  // 4. Call main namespace load function
  ns::clojure_core_ns_load_XXX{}.call();
}
```

The crash suggests that step 4 (or code within step 4) is accessing constants **before** they're initialized in step 3, leading to garbage memory being read.

## Files Examined

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/behavior/metadatable.cpp` - validate_meta
2. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/rtti.hpp` - try_object error message
3. `/tmp/jank-debug-*.cpp` - Generated JIT code

## Nil Guard Changes Made

Applied nil guards to `src/vybe/sdf/ios.jank`:
```clojure
;; Camera sync (with nil protection for JIT edge cases)
(defn sync-camera-to-cpp! []
  (let [cam (or (state/get-camera) {})]
    (sdfx/set_camera_distance (cpp/float. (or (:distance cam) 8.0)))
    (sdfx/set_camera_angle_x (cpp/float. (or (:angle-x cam) 0.3)))
    (sdfx/set_camera_angle_y (cpp/float. (or (:angle-y cam) 0.0)))
    (sdfx/set_camera_target_y (cpp/float. (or (:target-y cam) 0.0)))
    nil))
```

**Note:** These nil guards are defensive measures but the underlying crash happens BEFORE `-main` is called - during module loading itself.

## Key Insight

The crash happens during `require` of a module, not during execution of `-main`. This means the bug is in how constants are being generated/initialized in the compile server's JIT code generation.

## What's Next

1. **Investigate compile server code generation** - Check if constants are being referenced before initialization
2. **Add debug output** - Log which module is being loaded when the crash occurs
3. **Check constant initialization order** - Ensure all `new (&const_XXX)` calls happen before any code that uses them
4. **Consider deferred constant initialization** - Maybe constants should be lazily initialized on first access

## Fix Applied: Clear Target Cache

**Root Cause #2:** Stale compiled modules in `target/` directory.

The previous error `Required namespace successfully, 0 module(s)` was caused by the jank module cache. The compile server was finding pre-compiled `.o` files and not recompiling.

**Fix:**
```bash
rm -rf target/
# or
make clean-cache
```

After clearing cache:
- Compile server now compiles **9 modules** instead of 0
- All modules compile successfully on the server
- Modules are received by the iOS client

## Current Status: Module Loading Crash

After fixing the cache issue, the iOS app crashes during module loading:

1. ✅ `vybe.sdf.math` - loaded
2. ✅ `vybe.sdf.state` - loaded
3. ❌ Crash occurs before loading next module

The crash happens on the iOS side when loading object files, not during compilation.

## Commands Used

```bash
# Run iOS JIT test
make ios-jit-sim-run

# Check logs
tail -f /tmp/ios-jit-test.log

# Check generated code
ls -la /tmp/jank-debug-*.cpp

# Clear module cache
rm -rf target/
```

## Related Files

- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp` - JIT code generation
- `/Users/pfeodrippe/dev/something/src/vybe/sdf/ios.jank` - iOS entry point (git-tracked source)
- `/Users/pfeodrippe/dev/something/SdfViewerMobile/jank-resources/` - Derived/copied files (NOT git-tracked)
- `/Users/pfeodrippe/dev/something/ai/20251212-jank-module-cache-fix.md` - Previous doc about cache issue
