# jank iOS AOT Implementation Plan

**Date:** 2025-12-21
**Status:** Research Complete - Implementation Pending

## Overview

This document outlines a detailed plan for implementing jank iOS AOT (Ahead-of-Time) compilation support. Since iOS prohibits JIT compilation, we need to generate C++ code from jank source and compile it with clang for arm64 iOS, similar to the existing WASM AOT approach.

## Key Finding: wasm-aot Already Generates Platform-Agnostic C++

After investigating the jank codebase, I discovered that **the existing `--codegen wasm-aot` mode already generates standard C++ that can be compiled by any C++ compiler**. The generated code is NOT WASM-specific - it's standard C++ that uses the jank runtime.

### Generated C++ Example

```bash
# Generate C++ from jank
/Users/pfeodrippe/dev/jank/compiler+runtime/build/jank \
    --codegen wasm-aot \
    --save-cpp \
    --save-cpp-path ./generated/test_aot.cpp \
    run test_aot.jank
```

Output message:
```
[jank] Saved generated C++ to: ./generated/test_aot.cpp
[jank] WASM AOT mode: skipping JIT compilation
```

### Generated Code Structure

The generated C++ includes:

1. **jank runtime headers** (lines 1-19):
```cpp
// WASM AOT generated code - requires jank runtime headers
#include <jank/runtime/context.hpp>
#include <jank/runtime/obj/jit_function.hpp>
#include <jank/runtime/core.hpp>
// ... more includes
```

2. **Function classes** - each jank function becomes a C++ class:
```cpp
namespace test_aot {
    struct vybe_test_aot_hello_23 : jank::runtime::obj::jit_function {
        // constructor with captured vars
        jank::runtime::object_ref call() final {
            // function body
        }
    };
}
```

3. **Module loader function** (extern "C"):
```cpp
extern "C" void* jank_load_test_aot(){
    return test_aot::jank_load_test_aot{ }.call().erase();
}
```

4. **Export wrappers** (generated in main.cpp for `^:export` functions):
```cpp
extern "C" double jank_export_my_function(double arg) {
    // Boxes arg, calls function, unboxes result
}
```

## The Challenge: Runtime Dependencies

The generated C++ **requires the full jank runtime**, which has significant dependencies:

### Dependencies Required

| Library | Purpose | iOS Complexity |
|---------|---------|----------------|
| **libjank.a** | jank runtime (78MB for WASM) | Must be cross-compiled |
| **Folly** | Facebook's core library | Complex iOS build |
| **Boost** | Multiple Boost libraries | Available via CocoaPods |
| **BDW-GC** | Boehm garbage collector | Can be built for iOS |
| **libfmt** | Formatting library | Easy to build |
| **jtl** | jank template library | Header-only |

### Failed Compilation Test

```bash
# Attempt to compile generated C++ for iOS
xcrun --sdk iphonesimulator clang++ -c -std=c++20 \
    -target arm64-apple-ios15.0 \
    -I/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp \
    -I/Users/pfeodrippe/dev/jank/compiler+runtime/third-party/folly \
    # ... more includes ...
    test_aot.cpp -o test_aot.o
```

**Error:**
```
folly/functional/Invoke.h:22:10: fatal error:
'boost/preprocessor/control/expr_iif.hpp' file not found
```

The third-party/boost directory in jank is incomplete for iOS.

## Implementation Options

### Option A: Build Full jank Runtime for iOS (Recommended)

**Effort:** High (1-2 weeks)
**Benefit:** Full jank functionality on iOS

Steps:
1. Build BDW-GC for iOS (has iOS support)
2. Build Boost for iOS (using boost-iosx or similar)
3. Build Folly for iOS (complex, may need patches)
4. Build jank runtime for iOS
5. Create xcframework with simulator + device architectures

### Option B: Create Minimal iOS Runtime

**Effort:** Medium (1 week)
**Benefit:** Smaller footprint, simpler

Steps:
1. Create stripped-down jank runtime without JIT
2. Remove Folly dependency (use standard C++ alternatives)
3. Keep only essential runtime types
4. Build minimal libjank_ios.a

### Option C: Static Code Generation (No Runtime)

**Effort:** High (2-3 weeks)
**Benefit:** No runtime dependency at all

Steps:
1. Modify jank codegen to generate standalone C++
2. Inline all runtime operations
3. Generate pure C++ that doesn't need libjank
4. This is a significant jank compiler change

## Recommended Path Forward

### Phase 1: WASM Build System for iOS (Week 1)

Since jank already has a working WASM build that compiles the runtime with emscripten, we can adapt this for iOS:

1. **Study the WASM CMake configuration:**
   ```
   /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/CMakeCache.txt
   ```

2. **Create iOS CMake toolchain:**
   ```cmake
   # ios-toolchain.cmake
   set(CMAKE_SYSTEM_NAME iOS)
   set(CMAKE_OSX_SYSROOT iphoneos)
   set(CMAKE_OSX_ARCHITECTURES arm64)
   ```

3. **Configure jank for iOS:**
   ```bash
   mkdir build-ios && cd build-ios
   cmake .. -DCMAKE_TOOLCHAIN_FILE=ios-toolchain.cmake \
            -Djank_target_ios=ON
   ```

### Phase 2: Dependency Resolution (Week 1-2)

1. **BDW-GC:** Has iOS support, should work with minor tweaks
2. **Boost:** Use [iOSBoostFramework](https://github.com/nicklockwood/iOSBoostFramework) or build manually
3. **Folly:** Most complex - may need to:
   - Port to iOS
   - Replace with simpler alternatives
   - Conditionally disable on iOS

### Phase 3: Integration (Week 2)

1. Generate C++ from vybe.sdf using wasm-aot
2. Compile generated C++ for iOS
3. Link with libjank_ios.a
4. Integrate into SdfViewerMobile

## Files Modified/Examined During Research

### jank Compiler Files

| File | Purpose |
|------|---------|
| `include/cpp/jank/util/cli.hpp` | Defines `codegen_type` enum |
| `src/cpp/jank/util/cli.cpp` | CLI option parsing |
| `include/cpp/jank/codegen/llvm_processor.hpp` | Defines `compilation_target` enum |
| `src/cpp/jank/codegen/processor.cpp` | C++ code generation |
| `src/cpp/jank/runtime/context.cpp` | WASM AOT handling, includes generation |
| `src/cpp/main.cpp` | Export wrapper generation |

### Key Code Locations

**WASM AOT includes generation** (context.cpp:350-392):
```cpp
if(is_wasm_aot) {
    cpp_out << "// WASM AOT generated code...\n";
    cpp_out << "#include <jank/runtime/context.hpp>\n";
    // ... more includes
}
```

**Export wrapper generation** (main.cpp:78-155):
```cpp
if(util::cli::opts.codegen == util::cli::codegen_type::wasm_aot) {
    // Scan for ^:export metadata
    // Generate extern "C" wrappers
}
```

## Test File Created

`SdfViewerMobile/test_aot.jank`:
```clojure
(ns vybe.test-aot)

(defn ^:export hello []
  (println "Hello from jank iOS AOT!"))

(defn ^:export add [a b]
  (+ a b))
```

Generated output: `SdfViewerMobile/generated/test_aot.cpp` (55 lines)

## Conclusion

**iOS AOT is technically feasible** using the existing `--codegen wasm-aot` mode. The main work is building the jank runtime for iOS, which requires:

1. Cross-compiling BDW-GC, Boost, and Folly for iOS
2. Building libjank.a for arm64-apple-ios
3. Integrating with the SdfViewerMobile Xcode project

This is a significant but achievable task. The key insight is that **no changes to jank's code generation are needed** - only build system work to create libjank_ios.a.

## Next Steps

1. Start with building BDW-GC for iOS simulator
2. Evaluate Boost iOS build options
3. Assess Folly iOS compatibility
4. Create CMake toolchain for iOS
5. Build minimal libjank_ios.a prototype

## Alternative: Use jank on macOS, Native C++ on iOS

For faster iteration, consider:

1. Develop with jank on macOS (full JIT support)
2. Export the SDF shader/scene data as pure C++ data structures
3. Use the native sdf_engine.hpp on iOS (which already works!)
4. This avoids the jank runtime on iOS entirely

The iOS app already runs with sdf_engine.hpp. The question is whether we need jank's dynamic capabilities on iOS, or if static C++ is sufficient.
