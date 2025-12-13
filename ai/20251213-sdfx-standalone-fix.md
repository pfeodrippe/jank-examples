# Session: cpp/sdfx symbol resolution in standalone app

## Problem

When running `make sdf` (dev mode), `cpp/sdfx` is accessible from nREPL.
When running the standalone app (`make sdf-standalone`), `cpp/sdfx` is NOT directly accessible, even though functions that use it (like `get-current-shader-name`) work fine.

Error: `Unable to find 'sdfx' within namespace '' while trying to resolve 'sdfx.get_current_shader_name'.`

## Root Cause Analysis

1. **Missing headers**: Standalone didn't bundle `sdf_engine.hpp` and dependencies
2. **JIT/AOT state sharing**: Even with headers, `static` variables create separate instances per translation unit

## JIT/AOT State Sharing Issue

When evaluating `(should-close?)` via nREPL in the standalone app (after including the header), the app was closing unexpectedly.

### Root Cause

The `get_engine()` function used `static Engine** g_engine_ptr = nullptr;` at file scope. With `static`, each translation unit (AOT and JIT) gets its own copy. When JIT compiles the include and later code, the JIT's `get_engine()` would return `nullptr`, causing `should_close()` to return `true`.

Worse, the JIT might override the AOT's inline function symbols, causing the AOT main loop to call the JIT version (which uses the JIT's `nullptr` pointer).

### Fix

Changed from file-scope `static` to function-local static:

```cpp
// Before (broken - internal linkage, each TU gets own copy)
static Engine** g_engine_ptr = nullptr;
inline Engine*& get_engine() {
    if (!g_engine_ptr) {
        g_engine_ptr = new Engine*(nullptr);
    }
    return *g_engine_ptr;
}

// After (correct - function-local static with vague linkage)
inline Engine*& get_engine() {
    static Engine* ptr = nullptr;
    return ptr;
}
```

Also applied same fix to `get_sampler()`.

## Final Decision: Don't Bundle C++ Headers for Standalone

After discussion, decided NOT to bundle `sdf_engine.hpp` and its dependencies (SDL3, Vulkan, shaderc headers) in the standalone app because:

1. **JIT/AOT state sharing is fragile** - Even with function-local statics, there may be other subtle incompatibilities
2. **Jank code is naturally reloadable** - Better to maximize jank, minimize C++
3. **Jank wrappers are the interface** - Pre-compiled functions like `should-close?`, `get-current-shader-name` work fine
4. **Simpler architecture** - Less bundling complexity, smaller app size

### Recommended Architecture

- **C++ layer**: Thin bindings to external libraries (SDL, Vulkan, ImGui)
- **Jank layer**: State management, business logic, UI composition
- **nREPL in standalone**: Call jank functions, NOT raw `cpp/sdfx.X`
- **Adding functionality**: Add jank wrapper in `vybe/sdf.jank`, rebuild standalone

### What Works in Standalone nREPL

```clojure
;; These work (pre-compiled jank wrappers):
(should-close?)
(get-current-shader-name)
(switch-shader! 1)

;; This does NOT work (requires JIT C++ compilation):
(cpp/sdfx.should_close)
```

## Files Modified

- `bin/run_sdf.sh` - Added comment explaining why we don't bundle sdfx headers
- `vulkan/sdf_engine.hpp` - Fixed ODR issues with `get_engine()` and `get_sampler()` (still useful for dev mode JIT)
