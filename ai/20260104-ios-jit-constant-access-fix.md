# iOS JIT Constant Access Fix

Date: 2026-01-04

## Summary

Fixed iOS JIT crash during `-main` execution. The app now works and displays the 3D hand!

## Root Cause

Nested functions in JIT modules were using direct global variable access for lifted constants instead of the `jank_constant_get` registry lookup.

**Example - Generated code BEFORE fix:**
```cpp
struct to_double_838 : jank::runtime::obj::jit_function {
  jank::runtime::object_ref call(jank::runtime::object_ref const x) final {
    // WRONG: Direct access to uninitialized global
    auto const call_966(jank::runtime::make_box(jank::runtime::add(const_967, x)));
    return call_966;
  }
};
```

**Generated code AFTER fix:**
```cpp
struct to_double_838 : jank::runtime::obj::jit_function {
  jank::runtime::object_ref call(jank::runtime::object_ref const x) final {
    // CORRECT: Registry lookup
    auto const call_966(jank::runtime::make_box(jank::runtime::add(
      jank::runtime::object_ref::from_ptr(jank_constant_get("vybe::sdf::math::const_967")), x)));
    return call_966;
  }
};
```

## The Bug

In `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`, the `wrap_constant_access` function was checking `target` instead of `owner_target`:

```cpp
// BEFORE (wrong):
if(target == compilation_target::module)

// AFTER (correct):
if(owner_target == compilation_target::module)
```

**Why this matters:**
- When compiling nested functions like `to_double_838`, `target` is `function`
- But `owner_target` is `module` (the top-level module being compiled)
- The function incorrectly skipped wrapping because `target != module`

## Fix Applied

File: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`
Line: ~3028

```cpp
jtl::immutable_string processor::wrap_constant_access(jtl::immutable_string const &simple_name)
{
  /* For module target, wrap constant access with registry lookup to avoid
   * iOS JIT ADRP relocation issues. Constants are stored in a runtime registry
   * and accessed via jank_constant_get() instead of BSS namespace-scope globals.
   * Use owner_target (not target) so nested functions also use registry lookup. */
  if(owner_target == compilation_target::module)  // Changed from 'target'
  {
    auto const ns{ runtime::module::module_to_native_ns(module) };
    return util::format("jank::runtime::object_ref::from_ptr(jank_constant_get(\"{}::{}\"))",
                        ns,
                        simple_name);
  }
  return simple_name;
}
```

## Debugging Process

1. Used `[jit-symbol]` log output to decode crash stack addresses:
   - Frames #4-#8: `vybe.sdf.math` (0x108f541a4-0x1090541a4)
   - Frames #9-#11: `vybe.sdf.ios` (0x1093f4000-0x1094f4000)

2. Identified crash was in `vybe.sdf.math` functions being called from `vybe.sdf.ios`

3. Formatted generated C++ code with `clang-format` to inspect

4. Found `to_double_838` using direct `const_967` access instead of `jank_constant_get`

5. Traced to `wrap_constant_access` checking wrong target variable

## Commands

```bash
# Rebuild jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang
export CXX=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Test iOS JIT
make ios-jit-clean && make ios-jit-sim-run
```

## Files Modified

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`
   - Changed `target` to `owner_target` in `wrap_constant_access()`

## Result

iOS JIT now works correctly! All 9 modules load and `-main` executes successfully, displaying the 3D hand in the simulator.

---

## Second Fix: nREPL Eval with `#cpp` Strings

### Issue

After module loading worked, evaluating functions via nREPL that use `#cpp` format strings (like `#cpp "FPS: %.1f"`) failed:

```
error: use of undeclared identifier 'vybe_sdf_ui_G__3874'
```

### Root Cause

In `gen(analyze::expr::cpp_call_ref)`, nested function code was only emitted for `module` and `function` targets:

```cpp
// BEFORE (wrong):
if((target == compilation_target::module || target == compilation_target::function)
   && !expr->function_code.empty())
```

The compile server uses `target == eval`, so nested functions were being REFERENCED but never DECLARED.

### Fix

File: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`
Line: ~2006

```cpp
// AFTER (correct):
/* Emit nested function code for all targets that generate C++ structs.
 * This includes eval mode used by the iOS compile server, not just module/function. */
if(!expr->function_code.empty())
{
  util::format_to(cpp_raw_buffer, "\n{}\n", expr->function_code);
}
```

### Summary

Two fixes were needed for iOS JIT:
1. `wrap_constant_access`: Use `owner_target` instead of `target` for constant registry lookup
2. `gen(cpp_call)`: Emit `function_code` for all targets, not just module/function
