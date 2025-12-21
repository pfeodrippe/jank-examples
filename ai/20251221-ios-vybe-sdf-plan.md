# iOS vybe.sdf AOT Compilation Plan

**Date:** 2025-12-21

## Problem Statement

The vybe.sdf modules use jank's header requires feature (`["header.h" :as alias :scope ""]`) which allows calling C++ functions from headers. However, this requires JIT compilation of C++ headers during AOT code generation.

**Issue:** jank's embedded clang fails to find C++ standard library headers (`<new>`) during AOT generation, preventing header requires from working.

**Error:**
```
error: error reading 'new': No such file or directory
1 error generated.
```

## Modules That Work

Modules without header requires compile successfully:
- `vybe.sdf.state` - Pure jank atoms/state management

## Modules That Fail

Modules with header requires fail during AOT generation:
- `vybe.sdf.render` - Uses `["vulkan/sdf_engine.hpp" :as sdfx :scope "sdfx"]`
- `vybe.sdf.ui` - Uses ImGui headers
- `vybe.sdf.shader` - Uses Vulkan headers
- `vybe.sdf.math` - Uses `["vybe/vybe_sdf_math.h" :as _ :scope ""]`
- `vybe.flecs` - Uses flecs headers

## Solution: iOS-Specific Modules Using cpp/raw

Instead of header requires, create iOS-specific modules that use `cpp/raw` to declare the C++ functions directly. This bypasses the JIT header compilation.

### Approach

1. **Create cpp/raw declarations** for all needed C++ functions
2. **AOT compile** the pure jank code
3. **Link** against the C++ implementation (sdf_engine.cpp, imgui, flecs)

### Example: Converting Header Requires to cpp/raw

**Before (header require):**
```clojure
(ns vybe.sdf.render
  (:require
   ["vulkan/sdf_engine.hpp" :as sdfx :scope "sdfx"]))

(defn init! [shader-dir]
  (sdfx/init shader-dir))
```

**After (cpp/raw):**
```clojure
(ns vybe.sdf.render-ios)

;; Declare C++ functions using cpp/raw
(cpp/raw "
namespace sdfx {
  extern void init(std::string const& shader_dir);
  extern void cleanup();
  extern bool should_close();
  extern void draw_frame();
  // ... more declarations
}
")

(defn init! [shader-dir]
  (let* [_ (cpp/sdfx.init (cpp/unbox :string shader-dir))]
    nil))
```

### Files to Create

1. **`src/vybe/sdf_ios.jank`** - Main iOS entry point
2. **`src/vybe/sdf/render_ios.jank`** - Rendering with cpp/raw
3. **`src/vybe/sdf/ui_ios.jank`** - ImGui UI with cpp/raw
4. **`src/vybe/sdf/shader_ios.jank`** - Shader management with cpp/raw

### Build Steps

1. AOT compile iOS modules:
   ```bash
   jank --codegen wasm-aot --module-path src --save-cpp \
     --save-cpp-path generated/vybe_sdf_ios.cpp \
     run src/vybe/sdf_ios.jank
   ```

2. Add to iOS project (project.yml):
   ```yaml
   - path: generated/vybe_sdf_ios.cpp
     compilerFlags: ["-std=c++20"]
   ```

3. Ensure C++ implementations are compiled and linked

### Alternative: Fix jank Clang Setup

The root cause could be fixed in jank by:
1. Setting `--sysroot` for the embedded clang
2. Adding C++ standard library include paths
3. Ensuring `SDKROOT` is respected

This would allow header requires to work for AOT generation on macOS.

## Next Steps

1. Create iOS-specific render module with cpp/raw declarations
2. Test AOT compilation of the module
3. Iterate on other modules
4. Full integration test on iPad

## Commands Used

```bash
# Test simple module (works)
jank --codegen wasm-aot --module-path src --save-cpp \
  --save-cpp-path /tmp/state.cpp run src/vybe/sdf/state.jank

# Test header require module (fails)
jank --codegen wasm-aot --module-path src -Ivulkan -Ivendor \
  --save-cpp --save-cpp-path /tmp/math.cpp run src/vybe/sdf/math.jank
```
