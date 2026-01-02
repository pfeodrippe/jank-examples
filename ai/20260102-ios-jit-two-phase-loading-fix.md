# iOS JIT Two-Phase Loading Fix

**Date**: 2026-01-02

## Summary

Implemented two-phase loading to fix the iOS JIT module loading crash. The original crash occurred after loading only 2 modules. Now all 9 modules load successfully.

## What Was Fixed

### Original Problem
iOS JIT crashed after loading 2 modules (vybe.sdf.math, vybe.sdf.state). The crash happened during object file loading when ORC JIT tried to resolve symbols from modules that weren't fully initialized.

### Root Cause
The original loading loop did load+execute immediately for each module:
```cpp
for(auto const &mod : response.modules) {
  load_object(...);      // Load object into JIT
  find_symbol(...);      // Get entry function
  fn_ptr();              // Execute immediately <- CRASH
}
```

When Module 3 loaded, ORC JIT's `addObjectFile()` tried to resolve symbols referencing Module 2's globals before Module 2's entry function had fully registered them.

### The Fix
Changed to two-phase loading in `loader.cpp:1151-1217`:

**Phase 1**: Load ALL object files into ORC JIT
**Phase 2**: Call ALL entry functions in dependency order

```cpp
// Phase 1: Load all object files and collect entry functions
std::vector<std::tuple<std::string, void *, std::string>> entry_functions;
for(auto const &mod : response.modules) {
  load_object(...);
  entry_functions.push_back({name, find_symbol(...), entry_sym});
}

// Phase 2: Execute all entry functions
for(auto const &[name, fn_addr, entry_sym] : entry_functions) {
  fn_ptr();  // Now all symbols are registered
}
```

## Results

**Before fix**:
- Modules loaded: 2 (crash on 3rd)
- Crash location: During `addObjectFile()` in ORC JIT

**After fix**:
- Phase 1: All 9 objects loaded successfully
- Phase 2: Entry functions called in order
  - vybe.sdf.math: SUCCESS
  - vybe.sdf.state: SUCCESS
  - vybe.util: CRASH (different issue)

## New Issue: validate_meta Crash

After the two-phase loading fix, a NEW crash occurs during vybe.util's entry function execution:

**Stack trace**:
```
var::with_meta -> validate_meta -> to_string -> panic
```

The metadata object passed to `with_meta()` has an invalid/corrupted type. This is a different issue from the module loading crash - it happens during execution, not loading.

This crash is documented in `ai/20260102-ios-jit-validate-meta-crash.md`.

## Files Modified

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp`
   - Lines 1151-1217: Implemented two-phase loading

## Commands Used

```bash
# Build jank compiler
cd /Users/pfeodrippe/dev/jank/compiler+runtime
SDKROOT=$(xcrun --show-sdk-path) CC=$PWD/build/llvm-install/usr/local/bin/clang CXX=$PWD/build/llvm-install/usr/local/bin/clang++ ./bin/compile

# Test iOS JIT
cd /Users/pfeodrippe/dev/something
rm -rf target/
make ios-jit-sim-run

# Check crash reports
ls ~/Library/Logs/DiagnosticReports/ | grep -i sdf
cat ~/Library/Logs/DiagnosticReports/SdfViewerMobile-JIT-Sim-*.ips | head -200
```

## What's Next

1. **Investigate the validate_meta crash** - The metadata constant is garbage during vybe.util entry function execution
2. **Check constant initialization order** - Ensure all `const_*` are initialized before the namespace load function uses them
3. **Consider lazy metadata** - Maybe metadata should be lazily evaluated instead of using global constants

## Key Insight

The two-phase loading pattern is essential for JIT systems with multiple interdependent modules:
- Phase 1: Register all symbols without executing anything
- Phase 2: Execute initialization code after all symbols exist

This is similar to how AOT works - all symbols are resolved at link time, then initialization runs in controlled order.
