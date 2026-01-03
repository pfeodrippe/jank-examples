# iOS JIT Error Tracing Improvements

## Date: 2026-01-03

## Summary
Implemented enhanced error tracing for iOS JIT to provide better source location information when type mismatch errors occur (like "expected real found nil").

## Changes Made

### 1. In-Memory JIT Symbol Table (loader.cpp)
- Added `jit_symbol_entry` struct to store symbol address ranges and names
- Added `jit_symbol_table` vector to store symbols in memory
- Added `lookup_jit_symbol(uintptr_t addr)` function to find symbol name for a given address
- Enabled via `JANK_IOS_JIT` flag

### 2. Enhanced Error Messages (rtti.hpp)
- Modified `try_object<T>` to include:
  - Current source location from source hint stack
  - JIT symbol decoding in stack traces (iOS only)
- Stack traces now show `[JIT] module_name @ 0xaddress` for JIT code

### 3. Source Location Tracking Infrastructure (meta.hpp/cpp)
- Added `current_source_hint()` function to get current execution context
- Added `source_hint_guard` RAII struct for automatic push/pop of source hints

### 4. Codegen Source Tracking (processor.cpp)
- Added emission of `source_hint_guard` at function entry
- Tracks file, module, line, and column for each function
- Controlled by `JANK_DEBUG_SOURCE_TRACKING` flag (disabled by default)

## Compile Flags

1. **`JANK_IOS_JIT`** - Enables JIT symbol lookup and logging (iOS JIT only)
   - Already defined in iOS JIT builds
   - Enables in-memory symbol table and stack trace decoding

2. **`JANK_DEBUG_SOURCE_TRACKING`** - Enables source location tracking at function entry
   - Must be explicitly defined to enable
   - Adds overhead - use only for debugging
   - When enabled, error messages will show which jank function was executing

## Usage

For iOS JIT builds, error messages will now show:
```
invalid object type (expected real found nil); value=nil

=== Current jank Source Location ===
File: src/vybe/sdf/ui.jank
Module: vybe.sdf.ui
Line 70, Column 1

=== C++ Stack Trace ===
  [JIT] jank_load_vybe_sdf_ui @ 0x117feb000
  [JIT] jank_load_vybe_sdf_ios @ 0x118034000
  ...
=== End Stack Trace ===
```

## Files Modified
- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/module/loader.hpp`
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp`
- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/core/meta.hpp`
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/core/meta.cpp`
- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/rtti.hpp`
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`

## Next Steps
- To enable source tracking, add `-DJANK_DEBUG_SOURCE_TRACKING` to compile flags
- The iOS JIT build already has `JANK_IOS_JIT` defined, so JIT symbol decoding is automatic
- Test by running the iOS app and triggering the nil error to see enhanced output
