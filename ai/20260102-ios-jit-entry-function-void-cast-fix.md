# iOS JIT Entry Function Void Cast Fix

**Date:** 2026-01-02

## What I Learned

### Root Cause Discovery
The `jank_load_XXX` entry functions in JIT-compiled modules return `void`, but multiple locations in the jank codebase were casting them as returning `object*` or `object_ref`. This caused undefined behavior where garbage values in the return register were interpreted as object pointers, leading to the error:
```
invalid object type (expected symbol found nil)
Exception caught while destructing binding_scope
```

### Why vybe.util Failed but vybe.sdf.state Worked
- **vybe.sdf.state**: Simple namespace with no `:require` clause - just `(ns vybe.sdf.state)` and defonce forms
- **vybe.util**: Has `(:require [clojure.string :as str])` which triggers additional code paths during namespace loading

The require clause in vybe.util causes the entry function to execute more complex code paths that exposed the void/object_ref mismatch bug.

### ALL Locations Fixed

1. **loader.cpp:1028-1029** (Phase 1 load_o function)
   ```cpp
   // Before:
   reinterpret_cast<object *(*)()>(load_fn_res)();
   // After:
   reinterpret_cast<void (*)()>(load_fn_res)();
   ```

2. **loader.cpp:1214-1217** (Phase 2 entry function calls)
   ```cpp
   // Before:
   auto fn_ptr = reinterpret_cast<object_ref (*)()>(fn_addr);
   // After:
   auto fn_ptr = reinterpret_cast<void (*)()>(fn_addr);
   fn_ptr();
   ```

3. **context.cpp:271-285** (iOS JIT remote eval path) - **NEW FIX**
   ```cpp
   // Before (wrong - always cast as object*):
   auto const fn_ptr = reinterpret_cast<object *(*)()>(fn_result.expect_ok());
   return fn_ptr();

   // After (distinguish by entry symbol prefix):
   if(entry_sym.starts_with("jank_load_"))
   {
     /* Namespace load - returns void */
     auto fn_ptr = reinterpret_cast<void (*)()>(fn_result.expect_ok());
     fn_ptr();
     return jank_nil();
   }
   else
   {
     /* Eval - returns object* */
     auto fn_ptr = reinterpret_cast<object *(*)()>(fn_result.expect_ok());
     return fn_ptr();
   }
   ```

4. **c_api.cpp:36** (Declaration mismatch)
   ```cpp
   // Before:
   extern "C" jank_object_ref jank_load_clojure_core_native();
   // After:
   extern "C" void jank_load_clojure_core_native();
   ```

5. **aot/processor.cpp:794** (Declaration mismatch)
   ```cpp
   // Before:
   extern "C" jank_object_ref jank_load_clojure_core_native();
   // After:
   extern "C" void jank_load_clojure_core_native();
   ```

6. **bin/ios-bundle:599 and :800** (iOS bundle script declarations)
   ```cpp
   // Before:
   extern "C" void* jank_load_clojure_core_native();
   // After:
   extern "C" void jank_load_clojure_core_native();
   ```

### Key Insight
The entry functions changed from returning `object_ref` to returning `void` at some point, but several call sites weren't updated. This is a subtle ABI mismatch that only manifests as random crashes depending on what garbage value happens to be in the return register.

### Generated Code Verification
The generated C++ code for dynamically loaded namespaces correctly uses `void`:
```cpp
extern "C" void jank_load_vybe_util$loading__()
extern "C" void jank_load_vybe_sdf_greeting$loading__()
```

## Commands I Ran

```bash
# Rebuild macOS jank compiler
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.1.sdk
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Rebuild iOS JIT library
make ios-jit-sim

# Test iOS JIT app
make ios-jit-sim-run
```

## Files Modified

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp` - Fixed void casts at lines 1028 and 1214
2. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/context.cpp` - Fixed iOS JIT entry function cast with prefix check
3. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/c_api.cpp` - Fixed declaration at line 36
4. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/aot/processor.cpp` - Fixed declaration at line 794
5. `/Users/pfeodrippe/dev/jank/compiler+runtime/bin/ios-bundle` - Fixed declarations at lines 599 and 800

## Result

✅ **FIXED**: The void/object* cast mismatch bug. Modules like `vybe.util` with `:require` clauses now load successfully.

## New Issue Discovered

After fixing the cast issue, a NEW crash appeared in `jank::runtime::to_string` with an invalid object type. This happens in the render loop when calling `str` on C++ function return values (`const char*`). The malformed path `vulkan_kimvulkan_kimhand_cigarettevulkan_kim` suggests issues with how `const char*` returns are being handled by the JIT.

This is a SEPARATE bug that needs further investigation:
- The `str` function is receiving arguments that include native pointers
- One of these is not being properly wrapped as a jank object
- The raw pointer is being interpreted as an object pointer with garbage in the type field

### Next Steps for New Issue
1. Investigate how `const char*` returns from C++ functions are wrapped in the iOS JIT path
2. Check if `native_pointer_wrapper` is being used correctly
3. Consider if `str` needs special handling for `const char*` to convert them to actual strings
