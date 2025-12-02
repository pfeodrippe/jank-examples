# Integrated Demo: Static Linking with Raylib + ImGui + JoltPhysics

**Date**: 2024-12-02

## What I Learned

### 1. jank Static Linking Pattern

For native builds with jank, use this combination of flags:
- `--jit-lib <name>` - Tell JIT about the library for symbol resolution
- `--link-lib <path>` - Link the static library (.a) or object file (.o)
- `--obj <path>` - Load object files directly for JIT
- `--framework <name>` - macOS frameworks (Cocoa, IOKit, OpenGL, etc.)
- `-L<path>` - Library search path
- `-I<path>` - Include path

Example working command:
```bash
jank \
    -L"$SOMETHING_DIR/vendor/raylib/distr" \
    --jit-lib raylib_jank \
    -I./vendor/raylib/distr \
    --link-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib_jank.a" \
    --obj vendor/imgui/build/imgui.o \
    --obj vendor/jolt_wrapper.o \
    --framework Cocoa \
    --framework IOKit \
    --framework OpenGL \
    --framework CoreVideo \
    --framework CoreFoundation \
    --module-path src \
    run-main my-integrated-demo -main
```

### 2. C++ Function Naming Conflicts

When defining C++ functions in `cpp/raw` that will be wrapped by jank `defn`:
- **Problem**: jank generates a variable with the same name as the wrapper function
- **Solution**: Prefix C++ functions with `native_` to avoid conflicts

Bad:
```cpp
inline int64_t entity_count() { return ...; }
// (defn entity-count [] (cpp/entity_count)) conflicts!
```

Good:
```cpp
inline int64_t native_entity_count() { return ...; }
// (defn entity-count [] (cpp/native_entity_count)) works!
```

### 3. Void Return Handling

jank cannot directly return void-returning C++ functions from defn wrappers:
- **Problem**: Functions like `ImGui::NewFrame()`, `ImGui::Render()` return void
- **Solution**: Create wrapper functions in cpp/raw that call the void function

```cpp
inline void native_imgui_new_frame() { ImGui::NewFrame(); }
inline void native_imgui_render() { ImGui::Render(); }
```

### 4. Type Literals

jank requires explicit float literals for C++ functions expecting double/float:
- Use `15.0` not `15` when passing to functions expecting double
- Integer literals cause "expected real found integer" errors

### 5. Static Variables and ODR Issues

Static variables in inline functions have separate instances when JIT-compiled by jank:
- **Problem**: `static std::vector<Entity> g_entities` has different instances in different inline function instantiations
- **Solution**: Use a pointer to heap-allocated data accessed through a function

Bad:
```cpp
static std::vector<Entity> g_entities;  // Different instances in JIT!

inline void add() { g_entities.push_back(...); }  // Uses one instance
inline void draw() { for (auto& e : g_entities) {...} }  // Uses different instance!
```

Good:
```cpp
static std::vector<Entity>* g_entities_ptr = nullptr;

inline std::vector<Entity>& get_entities() {
    if (!g_entities_ptr) g_entities_ptr = new std::vector<Entity>();
    return *g_entities_ptr;
}

inline void add() { get_entities().push_back(...); }
inline void draw() { for (auto& e : get_entities()) {...} }
```

### 6. Environment Variables for macOS

Required environment variables:
```bash
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"
```

## What I Did

### 1. Created Working Static-Linked Integrated Demo

Combined three libraries:
- **Raylib** - 2D rendering via static lib `libraylib_jank.a`
- **ImGui** - Custom rlgl renderer via object files
- **JoltPhysics** - 3D physics via wrapper object file

### 2. Updated run_integrated.sh

```bash
#!/bin/bash
set -e
cd "$(dirname "$0")"

export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# Collect ImGui object files
OBJ_ARGS=""
for f in vendor/imgui/build/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Collect Jolt object files
OBJ_ARGS="$OBJ_ARGS --obj vendor/jolt_wrapper.o"
for f in vendor/JoltPhysics/distr/objs/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

jank \
    -L"$SOMETHING_DIR/vendor/raylib/distr" \
    --jit-lib raylib_jank \
    --link-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib_jank.a" \
    $OBJ_ARGS \
    --framework Cocoa \
    --framework IOKit \
    --framework OpenGL \
    --framework CoreVideo \
    --framework CoreFoundation \
    --module-path src \
    run-main my-integrated-demo -main
```

### 3. Demo Features

- Physics balls falling under gravity (JoltPhysics)
- Top-down 2D view (X/Z plane)
- Height shown as text and color intensity
- ImGui panel with: FPS, body count, pause, time scale, spawn controls
- Pan with mouse drag, zoom with scroll wheel
- Space to spawn new balls
- All with static linking (no dylibs)

## Commands Reference

```bash
# Run integrated demo
./run_integrated.sh

# Build ImGui (if needed)
./build_imgui.sh

# Build Jolt (if needed)
./build_jolt.sh
```

## What's Next / Future Work

### 1. WASM Version

Create `run_integrated_wasm.sh` following the pattern from `run_imgui_wasm.sh`:
- Use `--native-lib` for JIT during AOT
- Use `--prelink-lib` for WASM object files
- Create HTML canvas wrapper

### 2. Header Requires for More Idiomatic jank

Currently using cpp/raw for most C++ interaction. Could potentially use:
```clojure
(ns my-integrated-demo
  (:require
   ["raylib.h" :as rl :scope ""]))
```

But void returns and complex types need careful handling.

### 3. Add More Physics Features

- Collision callbacks
- Different body shapes (boxes, capsules)
- Constraints and joints

## File Structure

```
src/
└── my_integrated_demo.jank  # Integrated demo

run_integrated.sh            # Native run script (static linking)

vendor/
├── imgui/build/*.o          # ImGui object files
├── raylib/distr/            # Raylib static lib + headers
├── JoltPhysics/distr/objs/  # Jolt object files
└── jolt_wrapper.o           # Jolt C wrapper
```
