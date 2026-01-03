# iOS JIT Debug Trace Implementation

## Date: 2026-01-03

## Problem
When exceptions occurred in JIT-compiled jank code on iOS, we only saw C++ stack traces which showed native frames but not the jank function call chain. This made debugging difficult.

## Solution
Implemented a debug execution trace system using RAII guards that track jank function calls.

## Key Files Modified

### 1. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/core/meta.hpp`
- Added `debug_trace_push()`, `debug_trace_pop()`, `debug_trace_dump()` functions
- Added `debug_trace_guard` RAII class that pushes on construction and pops on destruction
- Added optional file/line parameters for future source location tracking

### 2. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/core/meta.cpp`
- Implemented thread-local trace stack with 32 entry limit (circular buffer)
- **Critical fix**: `debug_trace_pop()` checks `std::uncaught_exceptions()` and skips popping during exception unwinding. This preserves the trace for the catch block to dump.
- `debug_trace_dump()` formats the trace output

### 3. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`
- Emit `debug_trace_guard` at the start of each function body
- Uses function name from the jank AST

### 4. `/Users/pfeodrippe/dev/something/SdfViewerMobile/sdf_viewer_ios.mm`
- Added `#include <jank/runtime/core/meta.hpp>`
- Uncommented catch blocks (were disabled for Xcode debugging)
- Added `jank::runtime::debug_trace_dump()` call in each catch block

## Key Technical Insight

The trace was initially empty because of **stack unwinding during exception propagation**. When a C++ exception is thrown:
1. The runtime unwinds the stack
2. RAII destructors run (including `debug_trace_guard` destructors)
3. Each destructor calls `debug_trace_pop()`, clearing the trace
4. By the time the catch block runs and calls `debug_trace_dump()`, the trace is empty

**Fix**: Check `std::uncaught_exceptions() > 0` in `debug_trace_pop()` and skip popping if we're in exception unwinding mode. This preserves the trace for the exception handler.

## Example Output

```
╔══════════════════════════════════════════════════════════════
║ C++ Standard Exception
╠══════════════════════════════════════════════════════════════
║ not a number: nil
║
║ Note: This is a C++ exception, not a jank exception.
...
=== jank Execution Trace (most recent last) ===
  0: -main
  1: run!
  2: draw
  3: update-uniforms!
=== End Execution Trace ===
```

## Build Commands

```bash
# Rebuild iOS jank library
cd /Users/pfeodrippe/dev/jank/compiler+runtime
ninja -C build-ios-sim-jit

# Run iOS JIT app
make ios-jit-sim-run
```

## Future Improvements

1. Add .jank file paths and line numbers to trace entries (source info not currently preserved through codegen)
2. Consider making trace a fixed-size ring buffer instead of vector for better memory behavior
3. Could add current_file_var lookup at runtime for file info when source not available at codegen time
