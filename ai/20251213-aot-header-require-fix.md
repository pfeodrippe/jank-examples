# Session: AOT Header Require JIT Compilation Fix

## Problem

When running the standalone app (`SDFViewer.app`), it crashed immediately with:
```
runtime/invalid-cpp-eval
error: Unable to compile the provided C++ source.
```

No location info was provided, making debugging difficult.

## Root Cause Analysis

1. **Initial Investigation**: Found leftover `(cpp/eita)` debug forms in `vybe/type.jank` - removed them, but error persisted.

2. **Deeper Investigation**: The error occurred BEFORE `-main` was called, during module loading.

3. **Root Cause Found**: In jank's `core_native.cpp`, the `register_native_header` function (handles header requires like `["vulkan/sdf_engine.hpp" :as sdfx]`) calls `eval_cpp_string` to JIT compile `#include <header>`.

   In AOT mode:
   - Namespace state (native_aliases map) starts empty
   - AOT code runs `register_native_header` during module load
   - Since native_aliases is empty, `add_native_alias` returns true (alias added)
   - `eval_cpp_string("#include <vulkan/sdf_engine.hpp>")` is called
   - Header file isn't bundled in standalone app
   - JIT compilation fails, app crashes

4. **Why dev mode works**: In JIT mode, the header file is available and can be compiled.

## Fix Applied

Modified `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp`:

Changed `register_native_header` to warn instead of throwing when JIT compilation fails:

```cpp
if(res.is_err())
{
  /* In AOT mode, headers may not be available for JIT compilation,
   * but the symbols are already compiled into the binary. We register
   * the alias anyway so that code referencing the alias can find it.
   * Only warn instead of throwing. */
  util::println("[warning] Failed to JIT compile header '{}' - symbols may already be AOT compiled",
                include_arg);
}
```

This allows AOT compiled apps to work even when headers aren't bundled - the symbols are already in the binary from AOT compilation.

## Files Modified

### jank compiler
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp` - Made header JIT failures non-fatal

### something project
- `src/vybe/type.jank` - Removed leftover `(cpp/eita)` debug forms (lines 7 and 1057)
- `src/vybe/sdf.jank` - Temporarily added debug println (removed after testing)

## Commands Used

```bash
# Find top-level cpp/ forms
grep -rn "^(cpp/" src/vybe/ --include="*.jank"

# Find error definition in jank
grep -r "invalid-cpp-eval" /Users/pfeodrippe/dev/jank/compiler+runtime

# Rebuild jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime && ./bin/compile

# Rebuild standalone
make sdf-standalone

# Test standalone
./SDFViewer.app/Contents/MacOS/SDFViewer
```

## Result

Standalone app now works. Shows warnings about headers not available for JIT (expected behavior), but continues execution using the AOT compiled symbols.

## Why No Location Info in Original Error

The `runtime_invalid_cpp_eval()` error in jank's error/runtime.cpp uses `read::source::unknown` - there's no source location being passed. This is because the error occurs during C++ compilation by LLVM/Clang, not during jank source parsing.

## What to Do Next

1. Consider upstreaming the jank fix (PR to jank repo)
2. Consider adding more context to C++ eval errors (which header failed, what namespace)
3. Consider a proper AOT mode flag in jank to skip JIT compilation entirely when symbols are known to be AOT compiled
