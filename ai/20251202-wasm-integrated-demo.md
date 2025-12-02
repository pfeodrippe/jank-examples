# WASM Build for Integrated Demo (Raylib + ImGui + Jolt + Flecs)

**Date**: 2024-12-02

## What I Learned

### 1. WASM Build Requires Native Objects for AOT + WASM Objects for Linking

When building for WASM with jank's `emscripten-bundle`, you need two sets of object files:

1. **Native objects** (`--native-obj`) - Used during AOT compilation (JIT phase)
2. **WASM objects** (`--prelink-lib`) - Linked into the final WASM binary

```bash
# Native objects for JIT during AOT compilation
--native-obj vendor/jolt_wrapper.o
--native-obj vendor/JoltPhysics/distr/objs/*.o
--native-obj vendor/flecs/distr/flecs.o

# WASM objects for final linking
--prelink-lib vendor/jolt_wrapper_wasm.o
--prelink-lib vendor/jolt_combined_wasm.a
--prelink-lib vendor/flecs/distr/flecs_wasm.o
```

### 2. Don't Use `--native-lib` for Object Files

`--native-lib` tries to load dynamic libraries (dylib/so). For object files, always use:
- `--native-obj` for native .o files (AOT compilation)
- `--prelink-lib` for WASM .o/.a files (WASM linking)

### 3. Memory Settings for Large Demos

For demos with physics + ECS + rendering, increase memory:
```bash
--em-flag "-sALLOW_MEMORY_GROWTH=1"
--em-flag "-sINITIAL_MEMORY=67108864"  # 64MB
```

### 4. Flecs C API Works in Both Native and WASM

The Flecs C API (`ecs_mini()`, `ecs_new()`, `ecs_fini()`) works identically in:
- Native builds (via `flecs.o`)
- WASM builds (via `flecs_wasm.o`)

No special WASM-specific code needed for Flecs.

## Commands I Ran

```bash
# Build WASM version of integrated demo
./run_integrated_wasm.sh

# The script does:
# 1. Collects native ImGui objects for AOT JIT
# 2. Collects native Jolt objects for AOT JIT
# 3. Includes native Flecs object for AOT JIT
# 4. Links WASM objects for final binary
# 5. Copies canvas HTML to build-wasm folder

# View in browser (server already running)
# http://localhost:8888/my_integrated_demo_canvas.html
```

## Files Created

```
run_integrated_wasm.sh              # WASM build script
src/my_integrated_demo_canvas.html  # Browser canvas HTML

# Generated in /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/
my_integrated_demo.js               # ES module loader
my_integrated_demo.wasm             # WebAssembly binary
my_integrated_demo_canvas.html      # Copied canvas HTML
```

## Key Script Pattern

```bash
#!/bin/bash
# run_integrated_wasm.sh

# Collect native objects for JIT during AOT
JOLT_NATIVE_OBJS=""
JOLT_NATIVE_OBJS="$JOLT_NATIVE_OBJS --native-obj $SOMETHING_DIR/vendor/jolt_wrapper.o"
for f in "$SOMETHING_DIR/vendor/JoltPhysics/distr/objs"/*.o; do
    JOLT_NATIVE_OBJS="$JOLT_NATIVE_OBJS --native-obj $f"
done

IMGUI_NATIVE_OBJS=""
for f in "$SOMETHING_DIR/vendor/imgui/build"/*.o; do
    IMGUI_NATIVE_OBJS="$IMGUI_NATIVE_OBJS --native-obj $f"
done

# Collect WASM objects for linking
IMGUI_LIBS=""
for f in "$SOMETHING_DIR/vendor/imgui/build-wasm"/*.o; do
    IMGUI_LIBS="$IMGUI_LIBS --prelink-lib $f"
done

RELEASE=1 ./bin/emscripten-bundle --skip-build \
    --native-lib raylib_jank \
    $IMGUI_NATIVE_OBJS \
    $JOLT_NATIVE_OBJS \
    --native-obj "$SOMETHING_DIR/vendor/flecs/distr/flecs.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/raylib/distr/libraylib.web.a" \
    $IMGUI_LIBS \
    --prelink-lib "$SOMETHING_DIR/vendor/jolt_wrapper_wasm.o" \
    --prelink-lib "$SOMETHING_DIR/vendor/jolt_combined_wasm.a" \
    --prelink-lib "$SOMETHING_DIR/vendor/flecs/distr/flecs_wasm.o" \
    --em-flag "-sUSE_GLFW=3" \
    --em-flag "-sASYNCIFY" \
    --em-flag "-sALLOW_MEMORY_GROWTH=1" \
    --em-flag "-sINITIAL_MEMORY=67108864" \
    "$SOMETHING_DIR/src/my_integrated_demo.jank"
```

## What's Next / Future Work

1. **Test WASM Demo in Browser** - Verify all features work:
   - Physics balls moving
   - ImGui panel interactive
   - Pan/zoom controls
   - Spawn new balls with space

2. **Optimize WASM Size** - Currently unoptimized, could add:
   - `-Os` for size optimization
   - Dead code elimination
   - Tree shaking

3. **Add Touch Controls** - For mobile browser support:
   - Touch drag for pan
   - Pinch zoom
   - Touch spawn button

4. **Performance Profiling** - Compare native vs WASM:
   - FPS differences
   - Physics step timing
   - Memory usage

## File Structure

```
src/
├── my_integrated_demo.jank         # Main demo (shared native/WASM)
└── my_integrated_demo_canvas.html  # Browser HTML canvas

run_integrated.sh                   # Native build script
run_integrated_wasm.sh              # WASM build script

vendor/
├── imgui/build/*.o                 # Native ImGui objects
├── imgui/build-wasm/*.o            # WASM ImGui objects
├── jolt_wrapper.o                  # Native Jolt wrapper
├── jolt_wrapper_wasm.o             # WASM Jolt wrapper
├── jolt_combined_wasm.a            # WASM Jolt library
├── flecs/distr/flecs.o             # Native Flecs
└── flecs/distr/flecs_wasm.o        # WASM Flecs

ai/
├── 20251202-flecs-integration.md   # Flecs integration notes
└── 20251202-wasm-integrated-demo.md # This file
```
