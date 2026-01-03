# iOS JIT Lifted Constants Bug - 2026-01-02

## Summary
iOS JIT fails with "not a libspec" error due to uninitialized constants in nested namespace loader functions.

## Error Message
```
not a libspec: [prependss restart-agent sort-by is-runtime-annotation? ...]
```
This happens during `vybe.util` module loading. The vector should be `[clojure.string :as str]` but reads garbage memory.

## Root Cause Analysis
1. **Nested Function Constants Not Declared**: In the generated JIT code, `clojure_core_ns_load_*` class uses constants like `const_75926` but they are NOT declared in the namespace scope.

2. **AOT vs JIT Difference**:
   - AOT: Constants are properly declared and initialized
   - JIT/eval: The nested ns_load function references constants that don't exist

3. **Code Flow**:
   - vybe.util ns form expands to create a nested `clojure_core_ns_load_*` function
   - This function uses constants for the require vector `[clojure.string :as str]`
   - In JIT mode, these constants are never declared or initialized
   - Reading them gets garbage (random memory that looks like clojure.core symbols)

## Fix Attempts Made
1. **Moved lifted_constants merge after declaration_str()** - processor.cpp:1363-1377
   - This ensures constants from nested functions are collected after build_body runs
   - However, the constants still aren't being lifted in eval target

## Files Modified
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` - Line 1348-1377

## Key Insight
The issue is that for `compilation_target::eval`, the nested function (`clojure_core_ns_load_*`) is generated with constants inside it that reference namespace-scoped variables (`const_75926`), but these variables are:
1. Not declared in the namespace
2. Not initialized in the entry function

## Next Steps
1. Investigate how constants are lifted during `declaration_str()` for eval target
2. Check if the nested function processor should be using a different constant handling approach
3. Compare with how AOT handles the same ns form

## Commands Used
```bash
# Build jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang
export CXX=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Run iOS JIT
make ios-jit-sim-run 2>&1 | tee /tmp/ios_jit_run.txt

# Check generated code
grep "const_75926" /tmp/jank-debug-dep-vybe_util.cpp
```

## Debug Files
- `/tmp/jank-debug-dep-vybe_util.cpp` - Generated C++ for vybe.util module
- `/tmp/ios_jit_run.txt` - iOS JIT build and run output
