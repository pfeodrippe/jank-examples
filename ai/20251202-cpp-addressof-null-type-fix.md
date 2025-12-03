# Learning: cpp/& Null Type Crash Fix in jank Compiler

**Date**: 2024-12-02

## What I Learned

### 1. The Problem

When using `cpp/&` on a function returning a reference and passing it to a header require function:

```clojure
(imgui/SliderFloat "TimeScale" (cpp/& (cpp/get_time_scale)) 0.1 3.0 0)
```

jank crashes with:
```
Assertion failed: (!isNull() && "Cannot retrieve a NULL type pointer")
find_best_arg_types_with_conversions at cpp_util.cpp:568
```

### 2. Root Cause

In `find_best_arg_types_with_conversions` at `cpp_util.cpp:568`, the code calls:
```cpp
auto const arg_type{ Cpp::GetNonReferenceType(arg_types[arg_idx + member_offset].m_Type) };
```

If `m_Type` is null (which can happen with certain `cpp/&` expressions involving function return values), `Cpp::GetNonReferenceType(null)` triggers a CppInterOp assertion.

### 3. The Fix

Added null type check before calling `GetNonReferenceType`:

```cpp
auto const raw_arg_type{ arg_types[arg_idx + member_offset].m_Type };
/* Skip if the arg type is null - this can happen with certain cpp/& expressions. */
if(!raw_arg_type)
{
  continue;
}
auto const arg_type{ Cpp::GetNonReferenceType(raw_arg_type) };
```

## Commands I Ran

```bash
# Investigated the crash
./run_integrated.sh 2>&1 | head -60

# Searched for addressof handling in jank
cd /Users/pfeodrippe/dev/jank
grep -rn "GetNonReferenceType\(arg_types" compiler+runtime/src/cpp/jank/analyze/

# Built jank (failed due to SDK path issues)
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build
ninja -j4
```

## Files Modified

1. **`/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/analyze/cpp_util.cpp`**
   - Added null type check at line 568

2. **`/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/operator/amp/pass-unary-function-result.jank`**
   - Test case for the fix

3. **`/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/operator_/amp/pass_unary_function_result.hpp`**
   - Header file for the test

## Build Issue Encountered

The jank build is failing due to SDK header path configuration:
```
error: <cstddef> tried including <stddef.h> but didn't find libc++'s <stddef.h> header.
```

This is unrelated to my fix - likely an Xcode/SDK update issue.

## What's Next

1. Fix the SDK path configuration for jank build:
   ```bash
   export SDKROOT=$(xcrun --show-sdk-path)
   export CC=$PWD/build/llvm-install/usr/local/bin/clang
   export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
   ```

2. Rebuild jank and verify the fix

3. Run the integrated demo to confirm `cpp/&` with `imgui/SliderFloat` works:
   ```clojure
   (imgui/SliderFloat "TimeScale" (cpp/& (cpp/get_time_scale)) 0.1 3.0)
   ```

4. Consider adding similar null checks to other places in processor.cpp that call `Cpp::GetNonReferenceType(arg_types[...].m_Type)`:
   - Line 610
   - Line 655
   - Line 665
   - Line 794

## Key Insight

The `cpp/&` operator creates a `cpp_builtin_operator_call` expression whose type is computed by `amp_type()`. When the inner expression's type cannot be determined (null), this propagates through the type system and eventually crashes in `find_best_arg_types_with_conversions`.

The fix adds defensive null checks at the crash sites, but a more thorough fix would ensure `expression_type()` never returns null or that `amp_type()` handles null input gracefully.

## Investigation: C++ Default Parameters

### Investigation Summary

I investigated whether jank supports C++ default parameters. Here's what I found:

### jank Infrastructure for Default Parameters (Already Exists!)

**Key discovery:** jank already has infrastructure for default parameters:

1. **cpp_util.cpp:552-553** - `find_best_arg_types_with_conversions` properly uses `GetFunctionRequiredArgs`:
   ```cpp
   auto const num_args{ Cpp::GetFunctionNumArgs(fn) };
   if(Cpp::GetFunctionRequiredArgs(fn) <= arg_count && arg_count <= num_args)
   {
     matching_fns.emplace_back(fn);
     max_arg_count = std::max<usize>(max_arg_count, num_args);
   }
   ```

2. **CppInterOp** - `Cpp::BestOverloadMatch` uses Clang's `Sema::AddOverloadCandidate` which correctly handles default parameters through C++ standard overload resolution.

3. **Codegen (codegen/processor.cpp:1596)** - The codegen iterates over `expr->arg_exprs` and generates only the provided arguments:
   ```cpp
   for(usize arg_idx{}; arg_idx < expr->arg_exprs.size(); ++arg_idx)
   ```
   The C++ compiler will fill in defaults automatically.

### Why It Might Not Work for SliderFloat

The issue may be **overloaded functions**. ImGui::SliderFloat has multiple overloads:
```cpp
bool SliderFloat(const char*, float*, float, float, const char* = "%.3f", ImGuiSliderFlags = 0);
```

When you omit trailing arguments, jank/CppInterOp need to resolve which overload to use. This resolution uses Clang's Sema which should work, but there may be edge cases.

### Test Files Created

1. **Header with default param functions:**
   `/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/function/default_params/default_params.hpp`

2. **Test for default parameters:**
   `/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/function/default-params/pass-default-params.jank`

### Next Steps to Verify

1. **Try calling SliderFloat with fewer args:**
   ```clojure
   (imgui/SliderFloat #cpp "Label" ptr 0.1 3.0)  ;; Try without format and flags
   ```

2. **If it fails**, capture the exact error message - it will reveal whether:
   - The overload resolution is failing
   - There's an issue with how jank calls `BestOverloadMatch`
   - Something else is wrong

3. **Run the test case** to see if simple default parameters work:
   ```bash
   jank -I./test run ./test/jank/cpp/function/default-params/pass-default-params.jank
   ```

### CppInterOp APIs for Default Parameters

- `Cpp::GetFunctionNumArgs(fn)` - Total number of parameters
- `Cpp::GetFunctionRequiredArgs(fn)` - Minimum required arguments (excludes defaulted params)
- `Cpp::GetDefaultArgument(fn, idx)` - Get the default value for a parameter (useful for nREPL docs)

### Showing Default Values in nREPL Docs

To show default values in eldoc/info, jank would need to:
1. Use `Cpp::GetFunctionRequiredArgs` to know which params have defaults
2. Use `Cpp::GetDefaultArgument` to get the default value string
3. Format like: `SliderFloat(label, v, v_min, v_max, format = "%.3f", flags = 0) -> bool`

This is a separate enhancement from making defaults work in calls.

## C++ Default Parameters Fix - COMPLETED

### The Bug

When calling C++ functions with fewer arguments than the total parameter count (using default parameters), jank would crash with a segfault at `cpp_util.cpp:574`.

**Root Cause:** In `find_best_arg_types_with_conversions`, the loop iterates up to `max_arg_count` (the maximum number of parameters from any matching overload), but `arg_types.size()` only contains the actually provided arguments. This caused out-of-bounds array access.

```cpp
// The loop iterates to max_arg_count (e.g., 6 for SliderFloat)
for(usize arg_idx{}; arg_idx < max_arg_count; ++arg_idx)
{
    // But arg_types only has 4 elements when using defaults!
    auto const raw_arg_type{ arg_types[arg_idx + member_offset].m_Type };  // CRASH!
}
```

### The Fix

Added bounds check at `cpp_util.cpp:566-571`:

```cpp
for(usize arg_idx{}; arg_idx < max_arg_count; ++arg_idx)
{
  /* If this argument index is beyond what we were given (i.e., the caller is using
   * default parameters for this position), skip to the next argument. */
  if(arg_idx + member_offset >= arg_types.size())
  {
    continue;
  }
  // ... rest of loop
}
```

### Verification

Tested successfully with:
```clojure
;; Only 4 args instead of 6 - uses defaults for format and flags
(imgui/SliderFloat #cpp "TimeScale" (cpp/& (cpp/get_time_scale)) 0.1 3.0)
```

The demo ran correctly with the TimeScale slider working as expected.

### Summary of All Fixes in cpp_util.cpp

1. **Line 566-571:** Bounds check for default parameters (prevents out-of-bounds access)
2. **Line 576-578:** Null type check for `cpp/&` expressions (prevents CppInterOp assertion)
