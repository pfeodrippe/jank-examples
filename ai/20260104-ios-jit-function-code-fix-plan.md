# iOS JIT function_code Emission Fix Plan

Date: 2026-01-04

## Problem Summary

My previous fix to emit `function_code` for all targets in `gen(cpp_call_ref)` broke desktop JIT compilation of clojure.core:

```
error: redefinition of 'clojure_core_G__96478'
```

**Root Cause**: The desktop JIT interpreter (CppInterOp) accumulates state across evaluations. When `function_code` (inline function definitions) is emitted for `eval` target, the same definitions appear in multiple evaluation chunks, causing redefinition errors.

## Current State

The fix at `processor.cpp:2006-2011`:
```cpp
/* Emit nested function code for all targets that generate C++ structs.
 * This includes eval mode used by the iOS compile server, not just module/function. */
if(!expr->function_code.empty())
{
  util::format_to(cpp_raw_buffer, "\n{}\n", expr->function_code);
}
```

**Original code was:**
```cpp
if((target == compilation_target::module || target == compilation_target::function)
   && !expr->function_code.empty())
```

## Analysis

| Target | Desktop JIT | iOS Compile Server | Needs function_code? |
|--------|-------------|-------------------|---------------------|
| `module` | Generate .cpp file | Load namespace | YES (both) |
| `function` | Nested function | Nested function | YES (both) |
| `eval` | Interpret locally | Send to iOS | NO (desktop) / YES (iOS) |

**Key insight**: For `eval` target, desktop and iOS have opposite requirements:
- **Desktop eval**: Code is interpreted locally by CppInterOp which already has the runtime. Emitting function_code causes redefinitions.
- **iOS eval**: Code is sent to iOS device and compiled fresh. function_code is needed because iOS doesn't have the definitions pre-loaded.

## Solution Options

### Option A: Add `remote_compilation` flag
Add a boolean flag to the processor that the compile server sets:

```cpp
// In processor.hpp
struct processor {
  bool remote_compilation{false};  // New flag
  // ...
};

// In processor.cpp gen(cpp_call)
if((target == compilation_target::module
    || target == compilation_target::function
    || remote_compilation)
   && !expr->function_code.empty())
{
  util::format_to(cpp_raw_buffer, "\n{}\n", expr->function_code);
}
```

**Pros**: Explicit, clean separation of concerns
**Cons**: Requires changes to header and all server call sites

### Option B: Use deduplication with `emitted_function_codes`
The processor already has an `emitted_function_codes` set (never used):

```cpp
if(!expr->function_code.empty())
{
  if(emitted_function_codes.insert(expr->function_code).second)
  {
    util::format_to(cpp_raw_buffer, "\n{}\n", expr->function_code);
  }
}
```

**Pros**: Simple change, no API modification
**Cons**: Per-processor deduplication, doesn't help across processor instances (desktop JIT creates new processor per form)

### Option C: Check `owner_target` like `wrap_constant_access`
Follow the same pattern as the first fix:

```cpp
if((owner_target == compilation_target::module
    || owner_target == compilation_target::function)
   && !expr->function_code.empty())
```

**Pros**: Consistent with existing fix pattern
**Cons**: Doesn't solve iOS single-expression eval (owner_target would be `eval`)

### Option D: Revert to original + fix compile server differently
Revert my change and modify the compile server to use `module` target for eval requests:

```cpp
// In server.hpp compile_single_expression()
// Change from:
codegen::processor cg_prc{ fn_expr, module_name, codegen::compilation_target::eval };
// To:
codegen::processor cg_prc{ fn_expr, module_name, codegen::compilation_target::module };
```

**Pros**: Minimal change to processor.cpp
**Cons**: Using `module` for single expression might have other side effects

## Chosen Solution: Option A (remote_compilation flag)

This is the cleanest approach because:
1. Explicitly distinguishes between local and remote compilation
2. No side effects on other targets
3. Clear intent in the code

## Implementation Plan

### Step 1: Add flag to processor.hpp
Add member variable:
```cpp
bool remote_compilation{false};
```

### Step 2: Update gen(cpp_call) in processor.cpp
Restore original check with new condition:
```cpp
if((target == compilation_target::module
    || target == compilation_target::function
    || remote_compilation)
   && !expr->function_code.empty())
{
  util::format_to(cpp_raw_buffer, "\n{}\n", expr->function_code);
}
```

### Step 3: Update compile server (server.hpp)
For eval requests, set the flag:
```cpp
// Line 657 - compile_single_expression
codegen::processor cg_prc{ fn_expr, module_name, codegen::compilation_target::eval };
cg_prc.remote_compilation = true;  // NEW

// Line 767 - native-source op
codegen::processor cg_prc{ wrapped_expr, module, codegen::compilation_target::eval };
cg_prc.remote_compilation = true;  // NEW
```

### Step 4: Rebuild and test
```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime
./bin/compile

# Test iOS JIT
make ios-jit-clean && make ios-jit-sim-run
```

### Step 5: Test nREPL eval
- Connect to localhost:5558
- Evaluate `draw-debug-ui!`
- Verify `#cpp` strings work

## Files to Modify

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/codegen/processor.hpp`
   - Add `bool remote_compilation{false};` member

2. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`
   - Update function_code emission check at line ~2006

3. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
   - Set `remote_compilation = true` at lines 657, 767

## Expected Outcome

1. Desktop JIT works (no redefinition errors when loading clojure.core)
2. iOS module loading works (function_code emitted for `module` target)
3. iOS single-expression eval works (function_code emitted due to `remote_compilation` flag)
4. nREPL evaluation of functions with `#cpp` strings works
