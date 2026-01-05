# iOS JIT remote_compilation Fix

Date: 2026-01-04

## Problem

After fixing the `wrap_constant_access` issue for iOS JIT constant registry lookups, the second fix to emit `function_code` for all targets broke desktop JIT compilation with:

```
error: redefinition of 'clojure_core_G__96478'
```

This happened because desktop JIT (CppInterOp) accumulates state across evaluations, and emitting `function_code` for `eval` target caused the same inline function definitions to appear in multiple evaluation chunks.

## Root Cause Analysis

| Target | Desktop JIT | iOS Compile Server | Needs function_code? |
|--------|-------------|-------------------|---------------------|
| `module` | Generate .cpp file | Load namespace | YES (both) |
| `function` | Nested function | Nested function | YES (both) |
| `eval` | Interpret locally | Send to iOS | NO (desktop) / YES (iOS) |

The key insight: Desktop JIT and iOS compile server both use `eval` target but have **opposite requirements** for `function_code` emission.

## Solution: `remote_compilation` Flag

Added a `remote_compilation` boolean flag to the codegen processor that distinguishes iOS compile server (needs `function_code`) from desktop JIT (doesn't need it).

### Files Modified

1. **`/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/codegen/processor.hpp`**
   - Added `bool remote_compilation{}` member to processor struct

2. **`/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`**
   - Updated `function_code` emission check at line ~2006:
     ```cpp
     if((target == compilation_target::module
         || target == compilation_target::function
         || remote_compilation)
        && !expr->function_code.empty())
     ```
   - **CRITICAL**: Added inheritance of flag in nested processor creation at line ~1348:
     ```cpp
     processor prc{ expr, module, fn_target, owner_target };
     prc.remote_compilation = remote_compilation;  // Inherit from parent
     ```

3. **`/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`**
   - Set `cg_prc.remote_compilation = true` at line 657 (compile_single_expression)
   - Set `cg_prc.remote_compilation = true` at line 767 (native-source operation)

## Critical Lesson: Nested Processor Inheritance

The initial fix only set `remote_compilation = true` in the compile server, but it still failed with:

```
error: use of undeclared identifier 'vybe_sdf_ui_G__410'
```

**Root cause**: When processing nested functions (like the lambda inside `draw-debug-ui!` that contains `#cpp "FPS: %.1f"`), a NEW processor is created:

```cpp
processor prc{ expr, module, fn_target, owner_target };
```

This new processor had `remote_compilation = false` by default, so the `function_code` wasn't emitted for the nested function.

**Fix**: Explicitly inherit the flag from the parent processor.

## Commands Run

```bash
# Build jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang
export CXX=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Kill old compile-server processes
pkill -f compile-server

# Restart iOS JIT
cd /Users/pfeodrippe/dev/something
make ios-jit-sim-run

# Test via nREPL
clj-nrepl-eval -p 5558 -H localhost --reset-session "(in-ns 'vybe.sdf.ui)"
clj-nrepl-eval -p 5558 -H localhost "(defn draw-debug-ui! ...)"  # SUCCESS!
```

## Verification

Successfully evaluated `draw-debug-ui!` function with `#cpp "FPS: %.1f"`:

```
=> #'vybe.sdf.ui/draw-debug-ui!
```

## Summary of iOS JIT Fixes (Complete)

1. **Fix 1**: `wrap_constant_access` - Use `owner_target` instead of `target` to check for module target when wrapping constant access with registry lookup (avoids iOS ADRP relocation issues)

2. **Fix 2**: `remote_compilation` flag - Distinguish iOS compile server from desktop JIT for `function_code` emission, with proper inheritance to nested processors
