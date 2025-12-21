# Fix: Native Header Requires for iOS/WASM AOT Compilation

**Date:** 2025-12-21

## Problem Summary

When using `--codegen wasm-aot` to compile jank code that uses native header requires like:
```clojure
(ns my-ns
  (:require
   ["vulkan/sdf_engine.hpp" :as sdfx :scope "sdfx"]))
```

The compilation fails with:
```
error: Failed to JIT compile native header require.
  Namespace: vybe.sdf.screenshot
  Alias: sdfx
  Header: vulkan/sdf_engine.hpp
```

## Root Cause Analysis

### The Existing Mechanism (Working)

In `src/cpp/jank/runtime/context.cpp` lines 388-403, jank already has code to emit `#include` directives for native headers in the generated C++:

```cpp
/* Include native headers from (:require ["header.h" :as alias]) */
auto const curr_ns{ current_ns() };
auto const native_aliases{ curr_ns->native_aliases_snapshot() };
if(!native_aliases.empty())
{
  for(auto const &alias : native_aliases)
  {
    cpp_out << "#include " << alias.include_directive.c_str() << "\n";
  }
}
```

### The Problem

In `src/cpp/clojure/core_native.cpp` line 474-492, `register_native_header_wasm` checks **compile-time** defines:

```cpp
object_ref register_native_header_wasm(...)
{
#if !defined(JANK_TARGET_WASM) && !defined(JANK_TARGET_IOS)
    // In native jank, just call the regular function
    return register_native_header(current_ns, alias, header, scope, include_directive);
#else
    // In WASM, only register metadata (JIT is not available)
    // ... registers alias without JIT ...
#endif
}
```

During `--codegen wasm-aot`, we're running **native jank** (not WASM/iOS), so:
1. `JANK_TARGET_WASM` is NOT defined
2. `JANK_TARGET_IOS` is NOT defined
3. The first branch is taken, calling `register_native_header`
4. `register_native_header` tries to JIT compile the header via Cling
5. Complex headers fail to JIT compile
6. The alias is never registered
7. The generated C++ has no `#include` for the native header

### What Should Happen

During `--codegen wasm-aot`:
1. The alias should be registered (metadata only)
2. JIT compilation should be skipped
3. The #include should be emitted in the generated C++
4. The target platform's compiler (Xcode/emscripten) compiles the header

## Solution

### Fix Location

File: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp`

### Changes Required

Modify `register_native_header_wasm` to check `util::cli::opts.codegen` at **runtime**:

```cpp
object_ref register_native_header_wasm(object_ref const current_ns,
                                       object_ref const alias,
                                       object_ref const header,
                                       object_ref const scope,
                                       object_ref const include_directive)
{
#if !defined(JANK_TARGET_WASM) && !defined(JANK_TARGET_IOS)
    // Check if we're doing WASM/iOS AOT code generation
    // In that case, skip JIT and just register metadata
    if(util::cli::opts.codegen == util::cli::codegen_type::wasm_aot)
    {
      // AOT mode: only register metadata, #include will be emitted in generated C++
      auto const ns_obj(try_object<ns>(current_ns));
      runtime::ns::native_alias alias_data{ runtime::to_string(header),
                                            runtime::to_string(include_directive),
                                            runtime::to_string(scope) };
      ns_obj->add_native_alias(try_object<obj::symbol>(alias), std::move(alias_data)).expect_ok();
      return jank_nil;
    }
    // Normal JIT mode: call the regular function
    return register_native_header(current_ns, alias, header, scope, include_directive);
#else
    // In WASM/iOS runtime, only register metadata (JIT is not available)
    auto const ns_obj(try_object<ns>(current_ns));
    runtime::ns::native_alias alias_data{ runtime::to_string(header),
                                          runtime::to_string(include_directive),
                                          runtime::to_string(scope) };
    ns_obj->add_native_alias(try_object<obj::symbol>(alias), std::move(alias_data)).expect_ok();
    return jank_nil;
#endif
}
```

### Include Required Header

Add at the top of core_native.cpp (if not already present):
```cpp
#include <jank/util/cli.hpp>
```

## Testing Plan

1. Build jank with the fix:
   ```bash
   cd /Users/pfeodrippe/dev/jank/compiler+runtime
   export SDKROOT=$(xcrun --show-sdk-path)
   export CC=$PWD/build/llvm-install/usr/local/bin/clang
   export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
   ./bin/compile
   ```

2. Test AOT compilation of vybe.sdf.render:
   ```bash
   cd /Users/pfeodrippe/dev/something
   /Users/pfeodrippe/dev/jank/compiler+runtime/build/jank \
     --codegen wasm-aot \
     --module-path src \
     -I vulkan \
     --save-cpp \
     --save-cpp-path /tmp/test_render.cpp \
     compile-module vybe.sdf.render
   ```

3. Verify the generated C++ contains:
   ```cpp
   /* Native headers from :require directives */
   #include <vulkan/sdf_engine.hpp>
   ```

4. Test full vybe.sdf compilation and run on iPad

## Implementation Progress

### Fix 1: Skip JIT compilation in wasm-aot mode (DONE)

Modified `register_native_header` to check for wasm-aot codegen mode and skip JIT compilation:

```cpp
if(jank::util::cli::opts.codegen == jank::util::cli::codegen_type::wasm_aot)
{
  return jank_nil;  // Skip JIT, alias already registered
}
```

**Result**: Native header requires no longer fail with "Failed to JIT compile". The alias is registered successfully.

### Issue 2: Symbol resolution still fails

Even with Fix 1, compilation fails at a later stage:
```
error: Unable to find 'sdfx' within namespace '' while trying to resolve 'sdfx.alloc_screenshot_cmd'
```

**Root cause**: In wasm-aot mode, jank skips JIT parsing the header, so it never learns what functions exist in the `sdfx` namespace. When jank tries to generate C++ code for `(sdfx/alloc_screenshot_cmd)`, it can't find the symbol.

### Fix 2 (TODO): Defer symbol resolution to target compiler

In wasm-aot mode, jank should:
1. Trust that the symbol exists (it will be provided by the #include)
2. Generate the C++ code with the correct namespace syntax (`sdfx::alloc_screenshot_cmd`)
3. Let the target compiler (Xcode/emscripten) validate the symbol

This requires changes to the analyzer/codegen to not validate native symbols in wasm-aot mode.

## Current Workaround

Until Fix 2 is implemented, use the existing `cpp/raw` approach with explicit function declarations (as done in `vybe_sdf_ios.jank`). This works because:
1. cpp/raw code is JIT compiled (simple declarations work)
2. jank learns the function signatures
3. Generated C++ calls these functions
4. Real implementations come from the header at iOS compile time

## Next Steps

1. Investigate how jank resolves native function calls in `analyze/` and `codegen/`
2. Add wasm-aot handling to defer symbol resolution
3. Ensure #include directives are emitted for ALL modules' native headers (not just current namespace)

## Why This Is Still The Right Direction

1. **Minimal change approach**: Each fix addresses a specific issue
2. **Correct abstraction**: Checking codegen mode at runtime handles the clojure.core pre-compilation issue
3. **Existing mechanism partially works**: The #include emission code in context.cpp works once symbols resolve
4. **Path to no workarounds**: Once Fix 2 is done, users won't need weak stubs or cpp/raw hacks
