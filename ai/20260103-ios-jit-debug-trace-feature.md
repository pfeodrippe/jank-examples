# iOS JIT Debug Trace Feature

## Date: 2026-01-03

## Summary
Implemented a debug execution trace feature for iOS JIT that shows the exact call chain when errors occur. This allows tracing "expected real found nil" errors back to the specific jank function.

## Problem
When errors like "expected real found nil" occur in iOS JIT, the C++ stack trace shows native function names but not the jank function call chain. We needed a way to know exactly which jank functions were being executed when the error occurred.

## Solution
Added a runtime debug trace that:
1. Stores a circular buffer of the last 32 function names visited
2. Uses RAII guards (`debug_trace_guard`) to push/pop on function entry/exit
3. Dumps the trace in error messages

## Files Modified

### In jank compiler (`/Users/pfeodrippe/dev/jank/compiler+runtime/`)

1. **include/cpp/jank/runtime/core/meta.hpp**
   - Added `debug_trace_push()`, `debug_trace_pop()`, `debug_trace_dump()` declarations
   - Added `debug_trace_guard` RAII struct

2. **src/cpp/jank/runtime/core/meta.cpp**
   - Implemented thread-local circular buffer with 32 entries
   - Implemented push/pop/dump functions

3. **include/cpp/jank/runtime/rtti.hpp**
   - Added `sb(runtime::debug_trace_dump())` to include trace in error messages

4. **src/cpp/jank/codegen/processor.cpp**
   - Added emission of `debug_trace_guard` at every function entry:
   ```cpp
   util::format_to(body_buffer,
                   R"(jank::runtime::debug_trace_guard __debug_trace{{ "{}" }};)",
                   util::escape(root_fn->name));
   ```

## Example Output

When an error occurs, the output now includes:
```
invalid object type (expected real found nil); value=nil
=== jank Execution Trace (most recent last) ===
  0: -main
  1: run!
  2: draw
  3: draw-debug-ui!
=== End Execution Trace ===

=== C++ Stack Trace ===
  [JIT] jank_load_vybe_util$loading__ @ 0x13c4ccc1c
  ...
=== End Stack Trace ===
```

This shows that the error happened in `draw-debug-ui!`, which was called from `draw`, which was called from `run!`, which was called from `-main`.

## Build Commands

After modifying jank compiler files:
```bash
# Rebuild desktop jank (compile server)
cd /Users/pfeodrippe/dev/jank/compiler+runtime
SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk ninja -C build

# Sync headers to iOS project
rsync -av /Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/ /Users/pfeodrippe/dev/something/SdfViewerMobile/jank-resources/include/jank/

# Run iOS JIT build
make ios-jit-clean && make ios-jit-sim-run
```

## Notes
- The trace always shows function names, not line numbers (line numbers would require source info which isn't always available)
- The trace is a call stack, so you can see exactly which function called which
- The circular buffer keeps only the last 32 entries to avoid memory growth
- This feature is always enabled (no compile flag needed)
