# iOS JIT ABI Sizeof Investigation

## Date: 2026-01-02

## Problem

iOS JIT still shows `validate_meta` corruption with type=160 (garbage value) even after:
1. Adding all defines to compile server (`persistent_compiler.hpp`, `server.hpp`)
2. Adding all defines to PCH script (`build-ios-pch.sh`)
3. Rebuilding jank compiler

## Observed Behavior

```
[loader] Phase 2 - Calling entry function for: vybe.util
[validate_meta] meta type=16 (persistent_array_map) ptr=0x13c21eac0
... (many successful validate_meta calls with type=16)
[validate_meta] meta type=160 (unknown) ptr=0x13c214880
[validate_meta] ERROR: corrupted meta type 160!
[validate_meta] First 16 bytes of object data (hex): a0 19 fd a 1 0 0 0 40 54 a6 17 1 0 0 0
```

## Key Observations

1. **Partial success**: First two modules load OK (`vybe.sdf.math`, `vybe.sdf.state`)
2. **Progressive failure**: Many `validate_meta` calls in `vybe.util` succeed before failure
3. **Type 160 = 0xa0**: This is garbage, not a valid object_type (valid range 0-50)

## Hypothesis

The corrupted pointer (0x13c214880) points to garbage memory, suggesting either:
1. Use-after-free
2. Wrong struct offset/layout between iOS lib and JIT-compiled code
3. Memory corruption from earlier operations

## Investigation Approach

### Added sizeof Debug Functions

Added `jank_debug_print_sizeof()` to print sizes of key types from iOS lib:

```cpp
// In c_api.cpp (JANK_IOS_JIT section)
extern "C" void jank_debug_print_sizeof() {
  std::cerr << "[ABI CHECK - iOS lib]\n";
  std::cerr << "  sizeof(object): " << sizeof(object) << "\n";
  std::cerr << "  sizeof(object_ref): " << sizeof(object_ref) << "\n";
  std::cerr << "  sizeof(native_persistent_hash_map): " << sizeof(native_persistent_hash_map) << "\n";
  std::cerr << "  sizeof(ns): " << sizeof(ns) << "\n";
  std::cerr << "  sizeof(var): " << sizeof(var) << "\n";
  std::cerr << "  sizeof(symbol): " << sizeof(symbol) << "\n";
  std::cerr << "  sizeof(persistent_array_map): " << sizeof(persistent_array_map) << "\n";
  std::cerr << "  IMMER_TAGGED_NODE: " << IMMER_TAGGED_NODE << "\n";
  std::cerr << "  JANK_IOS_JIT: 1\n";
  std::cerr << "  JANK_TARGET_IOS: 1\n";
}
```

### Call from iOS App

Added call in `sdf_viewer_ios.mm`:
```cpp
#if defined(JANK_IOS_JIT)
  extern "C" void jank_debug_print_sizeof();
  jank_debug_print_sizeof();
```

## Next Steps

1. **Rebuild jank** with debug function:
   ```bash
   cd /Users/pfeodrippe/dev/jank/compiler+runtime
   export SDKROOT=$(xcrun --show-sdk-path)
   export CC=$PWD/build/llvm-install/usr/local/bin/clang
   export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
   ./bin/compile
   ```

2. **Clean and run iOS JIT**:
   ```bash
   make ios-jit-clean
   make ios-jit-sim-run 2>&1 | tee /tmp/ios_jit_build.txt
   ```

3. **Compare sizes**: Look for `[ABI CHECK - iOS lib]` output showing sizes

4. **Add JIT-side check**: Create a jank function that prints the same sizes from JIT-compiled code:
   ```clojure
   ;; In vybe.sdf.ui or similar
   (cpp/raw "void"
     "std::cerr << \"[ABI CHECK - JIT code]\\n\";
      std::cerr << \"  sizeof(object): \" << sizeof(jank::runtime::object) << \"\\n\";
      // ... same checks
     ")
   ```

## Files Modified

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/c_api.cpp`
   - Added `jank_debug_print_sizeof()` function
   - Added includes for ns, symbol, var, persistent_array_map, detail/type.hpp

2. `/Users/pfeodrippe/dev/something/SdfViewerMobile/sdf_viewer_ios.mm`
   - Added call to `jank_debug_print_sizeof()` at startup

## Potential Root Causes to Investigate

### 1. Build Type Mismatch (Debug vs Release)

Looking at Makefile:
- iOS lib (libjank.a): Built with `Debug` configuration
- Xcode project: Also uses `Debug` configuration

IMMER_TAGGED_NODE defaults:
- Debug (!NDEBUG): IMMER_TAGGED_NODE = 1
- Release (NDEBUG): IMMER_TAGGED_NODE = 0

Both should be consistent with explicit `-DIMMER_TAGGED_NODE=0`.

### 2. Missing Define in Some Path

All defines that must match everywhere:
| Define | iOS lib | Compile Server | PCH | Xcode |
|--------|---------|----------------|-----|-------|
| JANK_IOS_JIT=1 | CMake | persistent_compiler.hpp | build-ios-pch.sh | project.pbxproj |
| JANK_TARGET_IOS=1 | CMake | persistent_compiler.hpp | build-ios-pch.sh | - |
| IMMER_HAS_LIBGC=1 | CMake | persistent_compiler.hpp | build-ios-pch.sh | - |
| IMMER_TAGGED_NODE=0 | CMake | persistent_compiler.hpp | build-ios-pch.sh | - |
| FOLLY_* | CMake | persistent_compiler.hpp | build-ios-pch.sh | - |

### 3. GC or Threading Issue

The error pattern (partial success, then failure) suggests:
- Memory could be getting corrupted over time
- GC might be moving/collecting objects incorrectly
- Thread safety issue when multiple modules interact

## Command Log

```bash
# Investigation commands run
grep -n JANK_IOS_JIT /Users/pfeodrippe/dev/jank/compiler+runtime/CMakeLists.txt
grep -n IMMER_TAGGED_NODE /Users/pfeodrippe/dev/jank/compiler+runtime/CMakeLists.txt

# Build and test
make ios-jit-clean
make ios-jit-sim-run 2>&1 | tee /tmp/ios_jit_build.txt
```

## Expected Output After Fix

The sizeof values from iOS lib and JIT code should match exactly. Any mismatch indicates ABI incompatibility.
