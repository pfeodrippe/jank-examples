# nREPL C++ Compilation Error Display Fix

**Date:** 2025-12-13

## Problem

When evaluating jank code that triggers C++ compilation errors via nREPL, the full error message was not appearing in the REPL output. For example, errors like:

```
input_line_287:8:1074: error: cannot initialize a variable of type 'VkDescriptorSet *'
(aka 'struct VkDescriptorSet_T **') with an lvalue of type 'VkDescriptorSet'
(aka 'struct VkDescriptorSet_T *')
```

Would not appear in the nREPL output at all.

## Root Cause (Two Issues)

### Issue 1: LLVM Buffer Not Flushed
LLVM's `llvm::errs()` is a `raw_fd_ostream` that has its own internal buffer separate from the C `FILE* stderr`. `fflush(stderr)` doesn't flush LLVM's buffer.

### Issue 2: Destructor Order Problem
The `scoped_stderr_redirect` destructor runs AFTER the error response has already been built and returned from `handle_eval`. The stderr content gets captured to the temp file, but by the time the destructor forwards it to `captured_out`, the response has already been sent.

## Solution

### Part 1: Flush LLVM's error stream
Added explicit `llvm::errs().flush()` calls immediately after every `llvm::logAllUnhandledErrors()`.

### Part 2: Add `flush()` method and call it before sending responses
Added a `flush()` method to `scoped_stderr_redirect` that manually reads and forwards captured stderr content. Call this BEFORE `emit_pending_output()` in all catch blocks to ensure stderr content is captured before the error response is sent.

## Files Changed

### `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/core.hpp`
- Added `void flush()` method declaration to `scoped_stderr_redirect`

### `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/core.cpp`
- Added `flush_impl()` method to `scoped_stderr_redirect::impl` with incremental read support
- Added `bytes_already_read` member to track what's been read
- Public `flush()` method calls `pimpl->flush_impl()`
- Destructor now calls `flush_impl()` instead of duplicating code

### `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/nrepl_server/ops/eval.hpp`
- Changed `scoped_stderr_redirect` from `const` to non-const
- Added `stderr_redirect.flush()` before each `emit_pending_output()` call in:
  - Signal recovery block
  - `catch(runtime::object_ref)`
  - `catch(jank::error_ref)` (two places)
  - `catch(std::exception)`
  - `catch(...)`

### Other files (llvm::errs().flush() additions):
- `context.cpp` - after error logging in `eval_cpp_string()` and compile paths
- `evaluate.cpp` - after error logging in JIT and cpp/raw paths
- `jit/processor.cpp` - after error logging in eval and module loading

## Result

Now when evaluating code with C++ errors, the full error message appears:

```
input_line_617:8:1000: error: cannot initialize a variable of type 'VkDescriptorSet *'
(aka 'struct VkDescriptorSet_T **') with an lvalue of type 'VkDescriptorSet'
(aka 'struct VkDescriptorSet_T *')
```

## Commands

```bash
# Build jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
./bin/compile

# Test in project
make sdf
clj-nrepl-eval -p 5557 '(in-ns (quote vybe.sdf.screenshot))'
clj-nrepl-eval -p 5557 '(defn test-fn [] (cpp/new cpp/VkDescriptorSet (sdfx/get_descriptor_set)))'
```
