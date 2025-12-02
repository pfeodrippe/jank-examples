# Fix: reset_meta Missing in WASM Generated Code

**Date**: 2024-12-02

## Problem

WASM build failed with error:
```
error: no member named 'reset_meta' in namespace 'jank::runtime'
```

The generated C++ code for `cpp/box` calls `jank::runtime::reset_meta(...)` but the header wasn't included.

## Root Cause

The WASM AOT code generation in `context.cpp` emits includes for jank runtime headers, but `<jank/runtime/core/meta.hpp>` was missing from the list. The codegen at `processor.cpp:1927` emits:
```cpp
jank::runtime::reset_meta(obj, meta);
```

But without the include, the compiler can't find the `reset_meta` function.

## Fix

Added the meta header to the WASM AOT generated code includes in `context.cpp`:

```cpp
cpp_out << "#include <jank/util/scope_exit.hpp>\n";
/* Include meta for reset_meta used by cpp/box */
cpp_out << "#include <jank/runtime/core/meta.hpp>\n";
```

**File**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/context.cpp` line 322-323

## Key Insight

The `reset_meta` function is declared in `<jank/runtime/core/meta.hpp>` and implemented in `meta.cpp`. The WASM build includes `meta.cpp` in `jank_runtime_common_sources`, so the implementation exists - the issue was just the missing header include in generated code.

## Testing

Manually patched the generated file to add the include and the WASM build succeeded:
```bash
./run_integrated_wasm.sh  # Build complete!
```

## Note

The native jank build has a separate issue (clang segfault when building PCH) that prevented automatic testing. The fix was verified by manually patching the generated file.
