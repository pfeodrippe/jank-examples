# Debugging Stack Traces in jank

**Date**: 2025-12-07

## Summary

**`--debug` does NOT help with native stack traces.** The stack traces are identical with or without it. Native jank currently lacks the source mapping that WASM has.

## Problem

When running `./run_integrated.sh`, we encountered a runtime error:

```
Uncaught exception: invalid object type (expected integer found real)
```

The stack trace showed only C++ runtime frames (like `jank::runtime::dynamic_call`), not jank source locations, making it difficult to find the bug.

## Root Cause

The error was in `render-draw-data` function at `src/my_integrated_demo.jank:512-515`:

```clojure
;; BEFORE (broken):
(rl/BeginScissorMode (- (->* cr .-x) (->* p .-x))
                     (- (->* cr .-y) (->* p .-y))
                     (- (->* cr .-z) (->* cr .-x))
                     (- (->* cr .-w) (->* cr .-y)))
```

The raylib function `BeginScissorMode(int x, int y, int width, int height)` expects **integers**, but the subtraction of floats returns a **real** (float). This caused the type mismatch error.

## Fix

Cast the float results to int:

```clojure
;; AFTER (fixed):
(rl/BeginScissorMode (int (- (->* cr .-x) (->* p .-x)))
                     (int (- (->* cr .-y) (->* p .-y)))
                     (int (- (->* cr .-z) (->* cr .-x)))
                     (int (- (->* cr .-w) (->* cr .-y))))
```

Compare with the C++ version which also needs explicit casts:
```cpp
BeginScissorMode((int)(cr.x-p.x), (int)(cr.y-p.y), (int)(cr.z-cr.x), (int)(cr.w-cr.y));
```

## Why Native Source Mapping Doesn't Work (Unlike WASM)

### What jank does:

1. **Emits `#line` directives** in generated C++ (`codegen/processor.cpp:668`):
   ```cpp
   #line 512 "src/my_integrated_demo.jank"
   ```

2. **`--debug` adds `-g`** flag to clang for debug symbols

### Why it doesn't help stack traces:

- `cpptrace` (jank's stack trace library) reads from the **compiled JIT binary** (`jank-phase-1`)
- The JIT code doesn't embed jank source locations in a way `cpptrace` can extract
- All we see is `jank::runtime::dynamic_call` repeated

### WASM vs Native comparison:

| Feature | WASM | Native (JIT) |
|---------|------|--------------|
| Source maps | JavaScript source maps work | `#line` not surfaced |
| Debug tools | Browser devtools show source | lldb might work (untested) |
| Stack traces | Maps to original jank source | Shows C++ frames only |

### Potential solutions for native:

1. **Include source in error messages** - The type checker (`rtti.hpp:41`) could use the object's `:jank/source` metadata

2. **Runtime call stack** - Push/pop jank source locations on entry/exit (overhead)

3. **Enhanced cpptrace** - Build address-to-source lookup table

## Debugging Techniques for jank (Current)

### 1. Flags in run_integrated.sh

```bash
./run_integrated.sh --debug     # Enable debug symbols (doesn't help stack traces)
./run_integrated.sh --save-cpp  # Save generated C++ to ./generated_cpp/
./run_integrated.sh --lldb      # Run with lldb debugger
```

### 2. Understanding the error source

The error `invalid object type (expected X found Y)` comes from `jank/runtime/rtti.hpp:36-41`:
- Thrown when `try_object<T>` is called with wrong type
- Common causes: passing float where int expected

### 3. Where to look for type mismatches

- **Array indices** - must be integers
- **Raylib/C functions** - many expect `int` for positions/sizes
- **C++ calls** - check expected parameter types in headers
- **Arithmetic** - `+`, `-`, `*`, `/` on floats return floats

### 4. jank metadata (source locations)

jank stores source location metadata on objects via `:jank/source`:
- Contains `:file`, `:module`, `:start`, `:end` with line/col
- Stored on vars and functions
- **Not surfaced in stack traces** - this is the gap vs WASM

## Commands Used

```bash
# Run and see error
./run_integrated.sh 2>&1 | head -80

# Check jank CLI options
jank --help | grep -A2 "debug\|save-cpp"

# Search for error source in jank compiler
grep -rn "expected.*found" /path/to/jank/include --include="*.hpp"
```

## What to Do Next

1. Look for other places where float-to-int conversions might be needed
2. When using raylib/C APIs, always check expected parameter types

## Changes Made to jank (2025-12-07)

### Added `#line` directives to more expression types

Modified `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`:

1. Added new helper for optional source:
   ```cpp
   static void emit_line_directive(jtl::string_builder &buffer, jtl::option<read::source> const &source)
   ```

2. Added `emit_line_directive` calls to:
   - `gen(analyze::expr::let_ref ...)` - let bindings
   - `gen(analyze::expr::if_ref ...)` - conditionals
   - `gen(analyze::expr::cpp_call_ref ...)` - native function calls
   - `gen(analyze::expr::cpp_cast_ref ...)` - type casts
   - `gen(analyze::expr::throw_ref ...)` - exceptions

### Verified `#line` directives are emitted

Test confirmed that `#line` directives are being emitted:
```
[DEBUG] #line 9 "/Users/pfeodrippe/dev/something/src/debug_test.jank"
[DEBUG] #line 12 "/Users/pfeodrippe/dev/something/src/debug_test.jank"
[DEBUG] #line 16 "/Users/pfeodrippe/dev/something/src/debug_test.jank"
...
```

### SUCCESS: lldb with JIT loader shows jank source locations!

Using lldb with `settings set plugin.jit-loader.gdb.enable on`:

```
./run_debug_test.sh --lldb
```

**Stack trace now shows jank source locations:**
```
frame #3:  ... at debug_test.jank:9:197   (trigger-type-error)
frame #7:  ... at debug_test.jank:17:31   (inner-fn calling trigger-type-error)
frame #11: ... at debug_test.jank:21:31   (middle-fn calling inner-fn)
frame #15: ... at debug_test.jank:25:31   (outer-fn calling middle-fn)
frame #19: ... at debug_test.jank:35:31   (-main calling outer-fn)
```

### cpptrace limitation

cpptrace's "basic JIT support" (v0.8.3) doesn't read DWARF line tables from JIT objects.
- The JIT object IS registered via `cpptrace::register_jit_object()`
- But line numbers aren't resolved
- Could file an issue asking for full DWARF line table support

### How to debug jank with source locations

**Use the `--lldb` flag** in run scripts:
```bash
./run_integrated.sh --lldb
./run_debug_test.sh --lldb
```

This automatically:
1. Enables `--debug` flag for debug symbols
2. Enables JIT loader: `settings set plugin.jit-loader.gdb.enable on`
3. Sets breakpoint on exceptions: `breakpoint set -n __cxa_throw`
4. Runs and shows backtrace on crash

**Manual lldb** (for custom scripts):
```bash
cat > /tmp/lldb_debug.txt << 'EOF'
settings set plugin.jit-loader.gdb.enable on
breakpoint set -n __cxa_throw
run
bt
EOF

lldb -s /tmp/lldb_debug.txt -- jank --debug [your args]
```

### Test files created

- `src/debug_test.jank` - Test file that triggers type error
- `run_debug_test.sh` - Script to run the test with debugging

### Sources:
- [cpptrace JIT support](https://github.com/jeremy-rifkin/cpptrace)
- [cpptrace Issue #226](https://github.com/jeremy-rifkin/cpptrace/issues/226) - jank JIT symbols
- [LLVM JIT Debugging](https://llvm.org/docs/DebuggingJITedCode.html)
- [LLVM Source Level Debugging](https://llvm.org/docs/SourceLevelDebugging.html)
- [GCC #line directive](https://gcc.gnu.org/onlinedocs/gcc-8.5.0/cpp/Line-Control.html)
