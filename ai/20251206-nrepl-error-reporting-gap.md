# nREPL Error Reporting Gap Analysis

**Date**: 2025-12-06

## The Problem

When evaluating code via nREPL (REPL), error messages are much less informative than when running the same code via `./run_integrated.sh` (script mode).

## Root Cause

jank has TWO separate error reporting systems:

### 1. Script Mode - Rich Visual Rendering
**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/error/report.cpp`

The `error::report(error_ref e)` function (line 533) uses ftxui to render beautiful formatted errors:
- Source code snippets (up to 6 lines context)
- Syntax highlighting
- Line numbers with visual underlines
- Color-coded headers ("error: " in red)
- Multiple notes with proper annotations

Output example:
```
┌─ internal/compilation ──────
error: Unable to find 'aget' in scope

 ╭─ /path/to/file.jank ──────
 │ 485 │         (dotimes [n (cpp/.-CmdListsCount dd)]
 │ 486 │           (let [cl (cpp/aget (cpp/.-CmdLists dd) (cpp/int. n))
 │     │                     ^^^^^^^^ unable to resolve
 │ 487 │                 vb (-> (cpp/.-VtxBuffer cl)
 ╰────────────────────────────
```

### 2. nREPL Mode - Simple One-Liner
**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/nrepl_server/engine.hpp`

The `format_error_with_location()` function (line 1014) produces a minimal format:
```cpp
std::string format_error_with_location(...) {
    formatted.append(prefix);          // e.g., "internal/compilation"
    formatted.append(" at (");
    formatted.append(location);        // e.g., "file.jank:486:21"
    formatted.append(").\n");
    formatted.append(message);         // e.g., "Unable to find 'aget' in scope"
    return formatted;
}
```

Output:
```
internal/compilation at (file.jank:486:21).
Unable to find 'aget' in scope
```

## Key Files

| File | Purpose |
|------|---------|
| `include/cpp/jank/error/report.hpp` | Declares `void report(error_ref e)` |
| `src/cpp/jank/error/report.cpp` | Rich ftxui-based error rendering (595 lines) |
| `include/cpp/jank/nrepl_server/ops/eval.hpp` | nREPL eval handler with error catch (lines 279-327) |
| `include/cpp/jank/nrepl_server/engine.hpp` | `format_error_with_location()` simple formatter (line 1014) |

## The Fix

### Option 1: Call error::report() from nREPL (Simple)

In `eval.hpp`, after catching `jank::error_ref`, add a call to `error::report(err)`:

```cpp
catch(jank::error_ref const &err)
{
    // ... existing setup ...

    // ADD THIS: Print rich error to stderr (gets forwarded to client)
    error::report(err);

    // ... rest of existing error handling ...
}
```

This prints the rich error to stdout/stderr which gets forwarded to the nREPL client.

**Pros**: Minimal change, uses existing code
**Cons**: Error goes to output stream, not structured nREPL message

### Option 2: Add report_to_string() (Better)

Create a new function in `error/report.cpp`:

```cpp
std::string report_to_string(error_ref e);
```

That renders the same rich output but to a string instead of stdout. Then use this in nREPL:

```cpp
catch(jank::error_ref const &err)
{
    // ...
    auto const rich_error_string = error::report_to_string(err);
    err_msg.emplace("err", rich_error_string);  // Send rich error to client
    // ...
}
```

**Pros**: Structured, error goes in proper nREPL field
**Cons**: More code changes, need to refactor report.cpp

### Option 3: Use format_error_data() (Already Exists!)

Looking at line 1163 in `engine.hpp`, there's already a `format_error_data()` function that produces more structured output. This could be enhanced to include source snippets.

## Implementation Notes

The nREPL error handler already:
1. Catches `jank::error_ref` (line 279)
2. Extracts source location (line 294-295)
3. Builds serialized error with notes (line 296)
4. Records exception for later retrieval (line 300)
5. Sends error response with basic message (line 309)

The gap is step 5 - it uses `format_error_with_location()` instead of the rich `error::report()` output.

## Quick Test Fix

Add this line at line 286 in `eval.hpp`:

```cpp
catch(jank::error_ref const &err)
{
    // ... signal handling ...
    update_ns();
    emit_pending_output();
    std::cerr << "err source line: " << err->source.start.line << '\n';

    // ADD THIS LINE:
    error::report(err);  // Print rich error

    // ... rest ...
}
```

And add include at top of file:
```cpp
#include <jank/error/report.hpp>
```

## Fix Applied

Added the following changes to `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/nrepl_server/ops/eval.hpp`:

1. Added include at line 13:
```cpp
#include <jank/error/report.hpp>
```

2. Added call to `error::report()` in the catch block (line 288-290):
```cpp
catch(jank::error_ref const &err)
{
    // ... signal handling ...

    /* Print rich error report to stdout (gets forwarded to nREPL client) */
    error::report(err);
    emit_pending_output();

    // ... rest of error handling ...
}
```

## Additional Fix Applied

### std::exception catch block (for JIT errors)

The initial fix only applied to `jank::error_ref` exceptions. However, JIT compilation errors throw `std::runtime_error` (in `jit/processor.cpp:298`), which was caught by a different handler.

Added to `eval.hpp` (line ~335 in `catch(std::exception const &ex)` block):

```cpp
#include <jank/error/runtime.hpp>  // Added at top

// In catch block:
/* Print rich error report to stdout (gets forwarded to nREPL client) */
auto const runtime_err(error::runtime_unable_to_load_module(ex.what()));
error::report(runtime_err);
emit_pending_output();
```

This creates an `error_ref` from the `std::exception` and prints the rich formatted error.

## What's Next

1. Rebuild jank (currently blocked by SDK/clang build issue)
2. Test with the demo to verify rich errors appear in REPL
3. Consider Option 2 (report_to_string) for cleaner integration if needed

## Commands to Build and Test

```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile
./bin/test
```

## Summary of Changes Made

1. **`include/cpp/jank/nrepl_server/ops/eval.hpp`**:
   - Added `#include <jank/error/report.hpp>` (line 13)
   - Added `#include <jank/error/runtime.hpp>` (line 14)
   - Added `error::report(err);` call in `catch(jank::error_ref const &err)` block (line 289)
   - Added `error::runtime_unable_to_load_module()` + `error::report()` in `catch(std::exception const &ex)` block (line 337-339)
