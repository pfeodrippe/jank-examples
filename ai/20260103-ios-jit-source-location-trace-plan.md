# Plan: Enhanced Debug Trace with Source Locations

## Goal
Add .jank file path and line numbers to the debug trace output.

## Current State
```
=== jank Execution Trace (most recent last) ===
  0: -main
  1: run!
  2: draw
  3: draw-debug-ui!
=== End Execution Trace ===
```

## Target State
```
=== jank Execution Trace (most recent last) ===
  0: -main @ src/vybe/sdf/ios.jank:45
  1: run! @ src/vybe/sdf/ios.jank:120
  2: draw @ src/vybe/sdf/ui.jank:85
  3: draw-debug-ui! @ src/vybe/sdf/ui.jank:70
=== End Execution Trace ===
```

## Implementation Steps

### 1. Update debug_trace_entry struct (meta.cpp)
- Add `file` and `line` fields to store source location

### 2. Update debug_trace_push function (meta.hpp/cpp)
- Add optional file and line parameters
- Store them in the entry

### 3. Update debug_trace_guard (meta.hpp)
- Add constructor that accepts file and line
- Pass them to debug_trace_push

### 4. Update codegen (processor.cpp)
- When source info is available, emit debug_trace_guard with file/line
- Format: `debug_trace_guard __debug_trace{ "func_name", "file.jank", 42 };`

### 5. Update debug_trace_dump (meta.cpp)
- Format output to show file:line when available

## Files to Modify
1. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/core/meta.hpp`
2. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/core/meta.cpp`
3. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`

## Build & Test
```bash
# Rebuild desktop jank
SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk ninja -C /Users/pfeodrippe/dev/jank/compiler+runtime/build

# Sync headers
rsync -av /Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/ /Users/pfeodrippe/dev/something/SdfViewerMobile/jank-resources/include/jank/

# Test
make ios-jit-clean && make ios-jit-sim-run
```
