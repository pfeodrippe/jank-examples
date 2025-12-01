# Raylib Static Linking - COMPLETE

## Goal
Create a truly static raylib integration where the final executable doesn't depend on any raylib dylib.

## Status: SUCCESS (2024-11-30)

TRUE static linking achieved! The compiled executable runs without any raylib dylib dependency.

## Solution

### The Problem
jank's AOT compilation needed separate control over:
1. **JIT symbol resolution** - requires loading dylib to resolve symbols during compilation
2. **AOT linking** - should link against static library, not create dylib dependency

The original `-l` option passed libraries to BOTH JIT and AOT linker, creating an unwanted dynamic dependency.

### The Fix
Added two new jank CLI options:
- `--jit-lib` - Load library into JIT only (for symbol resolution during compilation)
- `--link-lib` - Pass library to AOT linker only (for static linking)

### Working Command
```bash
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"
export DYLD_LIBRARY_PATH="/Users/pfeodrippe/dev/something/vendor/raylib/distr:$DYLD_LIBRARY_PATH"

# Compile
jank \
  -L/Users/pfeodrippe/dev/something/vendor/raylib/distr \
  --jit-lib raylib_jank \
  --link-lib /Users/pfeodrippe/dev/something/vendor/raylib/distr/libraylib_jank.a \
  --framework Cocoa \
  --framework IOKit \
  --framework OpenGL \
  --framework CoreVideo \
  --framework CoreFoundation \
  --module-path /Users/pfeodrippe/dev/something/src \
  compile my-raylib \
  -o /tmp/raylib_jank_static

# Run
jank \
  -L/Users/pfeodrippe/dev/something/vendor/raylib/distr \
  --jit-lib raylib_jank \
  --link-lib /Users/pfeodrippe/dev/something/vendor/raylib/distr/libraylib_jank.a \
  --framework Cocoa \
  --framework IOKit \
  --framework OpenGL \
  --framework CoreVideo \
  --framework CoreFoundation \
  --module-path /Users/pfeodrippe/dev/something/src \
  run-main my-raylib
```

### Verify No Dylib Dependency
```bash
otool -L /tmp/raylib_jank_static | grep raylib
# Should return nothing - raylib is statically linked
```

## Required Files

### Static Library
Created combined static library containing both wrapper and raylib:
```bash
# Extract raylib.a objects
mkdir -p /tmp/raylib_extract && cd /tmp/raylib_extract
ar -x vendor/raylib/distr/libraylib.a

# Create combined library
ar rcs vendor/raylib/distr/libraylib_jank.a \
  vendor/raylib/distr/raylib_jank_wrapper.o \
  /tmp/raylib_extract/*.o
```

### macOS Frameworks Required
- Cocoa
- IOKit
- OpenGL
- CoreVideo
- CoreFoundation

## jank Modifications

**Files modified in `/Users/pfeodrippe/dev/jank/compiler+runtime`:**

### `include/cpp/jank/util/cli.hpp`
Added:
```cpp
native_vector<jtl::immutable_string> jit_libs;   // JIT only (not AOT linker)
native_vector<jtl::immutable_string> link_libs;  // AOT linker only (not JIT)
native_vector<jtl::immutable_string> frameworks; // macOS frameworks
```

### `src/cpp/jank/util/cli.cpp`
Added CLI options:
- `--jit-lib` - Libraries for JIT symbol resolution only
- `--link-lib` - Libraries for AOT linker only (static linking)
- `--framework` - macOS frameworks

### `src/cpp/jank/jit/processor.cpp`
Load `jit_libs` into JIT for symbol resolution:
```cpp
auto const &jit_load_result{ load_dynamic_libs(util::cli::opts.jit_libs) };
```

### `src/cpp/jank/aot/processor.cpp`
Pass `link_libs` to AOT linker using `-Wl,` prefix to avoid clang treating them as source files:
```cpp
for(auto const &lib : util::cli::opts.link_libs)
{
  if(lib.size() > 0 && lib[0] == '/')
    compiler_args.push_back(strdup(util::format("-Wl,{}", lib).c_str()));
  else
    compiler_args.push_back(strdup(util::format("-l{}", lib).c_str()));
}
```

Pass frameworks to linker:
```cpp
for(auto const &framework : util::cli::opts.frameworks)
{
  compiler_args.push_back(strdup("-framework"));
  compiler_args.push_back(strdup(framework.c_str()));
}
```

### Rebuild jank
```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile
```

## File Locations

- `src/my_raylib.jank` - Demo jank code
- `vendor/raylib/distr/libraylib_jank.dylib` - Dynamic library (for JIT)
- `vendor/raylib/distr/libraylib_jank.a` - Static library (for AOT)
- `vendor/raylib/distr/libraylib.a` - Original raylib static library
- `vendor/raylib/distr/raylib_jank_wrapper.o` - Wrapper object file
- `vendor/raylib/distr/raylib.h` - Raylib header

## Dynamic Version (Still Works)
```bash
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" \
DYLD_LIBRARY_PATH="/Users/pfeodrippe/dev/something/vendor/raylib/distr" \
jank -L/Users/pfeodrippe/dev/something/vendor/raylib/distr \
  -lraylib_jank \
  --module-path src \
  run-main my-raylib
```
