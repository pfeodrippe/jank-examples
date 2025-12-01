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
- `vendor/raylib/distr/libraylib.web.a` - WASM static library (for Emscripten)
- `vendor/raylib/distr/raylib_jank_wrapper.o` - Native wrapper object file
- `vendor/raylib/distr/raylib_jank_wrapper_wasm.o` - WASM wrapper object file
- `vendor/raylib/distr/raylib.h` - Raylib header
- `my_raylib_canvas.html` - HTML template for WASM canvas rendering (in project root)

## Dynamic Version (Still Works)
```bash
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" \
DYLD_LIBRARY_PATH="/Users/pfeodrippe/dev/something/vendor/raylib/distr" \
jank -L/Users/pfeodrippe/dev/something/vendor/raylib/distr \
  -lraylib_jank \
  --module-path src \
  run-main my-raylib
```

## WASM Support - SUCCESS

Raylib also works in WebAssembly! The build generates `.js`, `.wasm`, and `.html` files that run in browsers.

### WASM Build Command
**IMPORTANT**: Use `RELEASE=1` to avoid "local count too large" WebAssembly error!

```bash
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"
export DYLD_LIBRARY_PATH="/Users/pfeodrippe/dev/something/vendor/raylib/distr:$DYLD_LIBRARY_PATH"

cd /Users/pfeodrippe/dev/jank/compiler+runtime

# Fast build (~7s) with ASYNCIFY_ONLY optimization
RELEASE=1 ./bin/emscripten-bundle --skip-build \
    -L /Users/pfeodrippe/dev/something/vendor/raylib/distr \
    --native-lib raylib_jank \
    --lib /Users/pfeodrippe/dev/something/vendor/raylib/distr/libraylib.web.a \
    --lib /Users/pfeodrippe/dev/something/vendor/raylib/distr/raylib_jank_wrapper_wasm.o \
    -I /Users/pfeodrippe/dev/something/vendor/raylib/distr \
    --em-flag "-sUSE_GLFW=3" \
    --em-flag "-sASYNCIFY" \
    --em-flag "-sASYNCIFY_ONLY=[]" \
    --em-flag "-sFORCE_FILESYSTEM=1" \
    /Users/pfeodrippe/dev/something/src/my_raylib.jank
```

The `-sASYNCIFY_ONLY=[]` limits async instrumentation, reducing link time from minutes to ~7 seconds.

### Key WASM Options
- `--native-lib raylib_jank` - Load dylib for JIT symbol resolution during AOT compilation
- `--lib libraylib.web.a` - WASM static library for Emscripten linking
- `--lib raylib_jank_wrapper_wasm.o` - WASM wrapper object file
- `--em-flag "-sUSE_GLFW=3"` - Use Emscripten's GLFW implementation
- `--em-flag "-sASYNCIFY"` - Enable async support for raylib's main loop
- `--em-flag "-sFORCE_FILESYSTEM=1"` - Enable filesystem access

### Generated Files
- `build-wasm/my_raylib.js` - ES module loader
- `build-wasm/my_raylib.wasm` - WebAssembly binary
- `build-wasm/my_raylib.html` - Browser test page (basic)

### Run in Browser
**Note**: The `--run` flag runs with Node.js which has no graphics context.
For actual raylib rendering, you must use a browser:

```bash
# Copy generated WASM files to something folder root
cp /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/my_raylib.js \
   /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/my_raylib.wasm \
   /Users/pfeodrippe/dev/something/

# Start local server from something folder
cd /Users/pfeodrippe/dev/something
python3 -m http.server 8888
# Open http://localhost:8888/my_raylib_canvas.html
```

### Note: -main Entry Point
Do NOT add `(-main)` at the end of your jank source file!
The WASM entry file now automatically calls `-main` after loading the module.
This prevents the raylib window from opening during compilation while still
running the application in the browser.

### WASM Wrapper
Create `raylib_jank_wrapper_wasm.o` by compiling the wrapper with emcc:
```bash
cd vendor/raylib/distr
emcc -c raylib_jank_wrapper.cpp -o raylib_jank_wrapper_wasm.o \
  -I. -DPLATFORM_WEB -std=c++20
```

### emscripten-bundle Modifications
Added to `/Users/pfeodrippe/dev/jank/compiler+runtime/bin/emscripten-bundle`:
- `--native-lib LIB` - Load native dylib for JIT during AOT compilation
- `-L DIR` - Add native library search directory
- `--em-flag FLAG` - Add extra flags to em++ linker
- Changed from `jank run` to `jank compile-module` to avoid evaluating top-level side effects
- Entry file now automatically calls `-main` after module load (no need to add it to source)
