# iOS AOT for jank - Complete Implementation

**Date:** 2025-12-21

## Summary

Successfully built libjank.a for iOS simulator (arm64) and integrated jank AOT-compiled code into the SdfViewerMobile iOS app. **BUILD SUCCEEDED**.

## Libraries Built

| Library | Size | Purpose |
|---------|------|---------|
| libjank.a | 25 MB | jank runtime |
| libjankzip.a | 322 KB | jank zip support |
| libgc.a | 780 KB | Boehm garbage collector |
| libnanobench.a | 404 KB | Benchmarking (optional) |

## Key Changes Made

### 1. jank CMakeLists.txt (iOS Source Selection)

Added iOS-specific source files with stubs for JIT:

```cmake
elseif(jank_target_ios)
  # iOS AOT build: runtime only, no JIT/LLVM dependencies
  message(STATUS "Building iOS runtime library (AOT only, no JIT)")
  list(APPEND jank_lib_sources ${jank_runtime_common_sources})
  list(APPEND jank_lib_sources
    src/cpp/jank/util/environment_wasm.cpp  # Uses WASM environment (no clang headers)
    src/cpp/jank/util/cli.cpp
    src/cpp/clojure/core_native.cpp
    src/cpp/clojure/string_native.cpp
    src/cpp/jank/wasm_native_stubs.cpp
    src/cpp/jank/wasm_stub.cpp           # JIT stubs
    src/cpp/jank/gc_wasm_stub.cpp        # GC stubs
    src/cpp/jank/runtime/perf.cpp
    src/cpp/jank/c_api_wasm.cpp
    src/cpp/jank/codegen/wasm_patch_processor.cpp
    src/cpp/jank/error/aot.cpp
  )
endif()
```

### 2. Preprocessor Guard Pattern

Changed all JIT-related guards from:
```cpp
#ifndef JANK_TARGET_WASM
// JIT code
#endif
```

To:
```cpp
#if !defined(JANK_TARGET_WASM) && !defined(JANK_TARGET_IOS)
// JIT code
#endif
```

And for includes/declarations:
```cpp
#if (!defined(JANK_TARGET_WASM) && !defined(JANK_TARGET_IOS)) || defined(JANK_HAS_CPPINTEROP)
  #include <jank/analyze/processor.hpp>
  #include <jank/jit/processor.hpp>
#endif
```

### 3. Files Modified in jank Compiler

| File | Changes |
|------|---------|
| `CMakeLists.txt` | iOS source file selection |
| `src/cpp/jank/runtime/context.cpp` | iOS guards for JIT, code generator for Obj-C++ nil macro |
| `include/cpp/jank/runtime/context.hpp` | iOS guards for jit_prc, an_prc members |
| `src/cpp/jank/read/parse.cpp` | iOS guard for an_prc.is_special() |
| `src/cpp/clojure/core_native.cpp` | iOS guards for JIT functions |

### 4. Objective-C++ `nil` Macro Conflict

iOS/Objective-C defines `nil` as a macro which conflicts with `jank::runtime::object_type::nil`.

**Solution in generated code:**
```cpp
#ifdef __OBJC__
#pragma push_macro("nil")
#undef nil
#endif

// ... jank includes ...

#ifdef __OBJC__
#pragma pop_macro("nil")
#endif
```

**Solution when including jank headers in .mm files:**
```cpp
#pragma push_macro("nil")
#undef nil
#include <jank/runtime/context.hpp>
#include <jank/runtime/core.hpp>
#pragma pop_macro("nil")
```

### 5. XcodeGen Project Configuration

Key settings in `project.yml`:

```yaml
settings:
  base:
    CLANG_CXX_LANGUAGE_STANDARD: c++20
    GCC_PREPROCESSOR_DEFINITIONS:
      - JANK_TARGET_IOS=1
      - IMMER_HAS_LIBGC=1
      - IMMER_TAGGED_NODE=0
    HEADER_SEARCH_PATHS:
      - /Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp
      - /Users/pfeodrippe/dev/jank/compiler+runtime/third-party/immer
      - /Users/pfeodrippe/dev/jank/compiler+runtime/third-party/bdwgc/include
      - /Users/pfeodrippe/dev/jank/compiler+runtime/third-party/bpptree/include
      - /Users/pfeodrippe/dev/jank/compiler+runtime/third-party/boost-preprocessor/include
      - /Users/pfeodrippe/dev/jank/compiler+runtime/third-party/folly
      - /Users/pfeodrippe/dev/jank/compiler+runtime/third-party/stduuid/include
    LIBRARY_SEARCH_PATHS:
      - /Users/pfeodrippe/dev/jank/compiler+runtime/build-ios
      - /Users/pfeodrippe/dev/jank/compiler+runtime/build-ios/third-party/bdwgc
    OTHER_LDFLAGS:
      - "-ljank"
      - "-ljankzip"
      - "-lgc"
```

## Build Commands

### Build jank for iOS

```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime

# Build iOS libraries
cmake -B build-ios \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ios-simulator-toolchain.cmake \
  -Djank_target_ios=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-ios -j8
```

### Generate AOT C++ from jank

```bash
/Users/pfeodrippe/dev/jank/compiler+runtime/build/jank \
    --codegen wasm-aot \
    --save-cpp \
    --save-cpp-path ./generated/test_aot.cpp \
    run test_aot.jank
```

### Build iOS App

```bash
cd SdfViewerMobile
xcodegen generate
xcodebuild -project SdfViewerMobile.xcodeproj \
    -scheme SdfViewerMobile \
    -sdk iphonesimulator \
    -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
    build
```

## jank Initialization in iOS

```cpp
// sdf_viewer_ios.mm
#pragma push_macro("nil")
#undef nil
#include <gc.h>
#include <jank/runtime/context.hpp>
#include <jank/runtime/core.hpp>
#pragma pop_macro("nil")

// AOT module entry point
extern "C" void* jank_load_test_aot();

static bool init_jank_runtime() {
    try {
        GC_init();
        jank::runtime::__rt_ctx = new (GC_malloc(sizeof(jank::runtime::context)))
            jank::runtime::context{};
        jank_load_test_aot();
        return true;
    } catch (const std::exception& e) {
        NSLog(@"jank init failed: %s", e.what());
        return false;
    }
}
```

## What I Learned

1. **iOS uses same AOT approach as WASM** - Both skip JIT, use pre-compiled code
2. **JANK_TARGET_IOS parallels JANK_TARGET_WASM** - Most guards can be combined
3. **BDW-GC works on iOS** - Just needs proper cross-compilation
4. **Objective-C++ nil macro** - Common pitfall, solved with pragma push/pop
5. **iOS deployment target 17.0** - Required for C++20 std::format support

## Test Results

**jank runtime initialization on iPad: SUCCESS!**

```
[jank] Initializing Boehm GC...
[jank] Creating runtime context...
[jank] Loading clojure.core native functions...
[jank] Loading clojure.core...
[jank] Loading test AOT module...
[jank] Runtime initialized successfully!
```

The app ran successfully on iPad Pro 13-inch (M4) simulator with:
- MoltenVK 1.2.9 / Vulkan 1.2.283
- 11 shaders loaded
- ImGui initialized
- Kim Kitsuragi SDF scene rendered (6 objects)

## Next Steps

1. **AOT compile vybe.sdf** - Replace test_aot with actual SDF viewer code
2. **Call jank functions from UI** - Wire up iOS touch events to jank
3. **Performance testing** - Compare iOS AOT vs macOS JIT
4. **Test on physical device** - Build for arm64-apple-ios (not simulator)
