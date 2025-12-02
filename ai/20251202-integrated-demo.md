# Integrated Demo: Raylib + ImGui + JoltPhysics

**Date**: 2024-12-02

## What I Learned

### 1. Flecs C++ Templates Can't Be JIT-Resolved

When using Flecs C++ API (`flecs::world`, `world->entity()`, `world->each()`), the compiler generates template-instantiated symbols like:
```
__ZN5flecs5queryIJR8PositionR10RenderableEED2Ev
__ZN5flecs1_13each_delegateIZ19run_integrated_demov...
```

These symbols are generated at compile time and can't be resolved by jank's JIT. The error manifests as:
```
JIT session error: Symbols not found: [ __ZN5flecs... ]
```

**Solutions**:
- Use Flecs C API instead (`ecs_world_new()`, `ecs_new()`, etc.)
- Create a precompiled C++ wrapper (like the Jolt wrapper)
- Use simple `std::vector` for entity storage (what we did)

### 2. Raylib 3D Model Functions Missing from dylib

The `libraylib_jank.dylib` doesn't include 3D model functions:
- `DrawSphere`, `DrawSphereWires`
- `DrawPlane`, `DrawGrid`
- `DrawCube`, `DrawModel`

These are in raylib's `rmodels` module which wasn't compiled into the dylib.

**Available 2D functions**:
- `DrawCircle`, `DrawCircleLines`
- `DrawRectangle`, `DrawLine`, `DrawLineV`
- `DrawText`

**Available rlgl primitives**:
- `rlBegin`, `rlEnd`, `rlVertex2f`, `rlVertex3f`
- `rlColor4ub`, `rlTexCoord2f`

### 3. Top-Down 2D Visualization Works for 3D Physics

Converting 3D physics to 2D view:
```cpp
// X/Z physics coords -> X/Y screen coords
Vector2 physicsToScreen(float px, float py, float pz) {
    return {
        viewOffset.x + px * viewScale,
        viewOffset.y - pz * viewScale  // flip Z for screen Y
    };
}
```

Height (Y) can be shown as:
- Text labels on each entity
- Color intensity (brighter = higher)
- Shadow offset

## What I Did

### 1. Created Integrated Demo (`src/my_integrated_demo.jank`)

Combined three libraries:
- **Raylib** - 2D rendering (circles, lines, text)
- **ImGui** - Debug UI panel
- **JoltPhysics** - Full 3D physics simulation

Used simple `std::vector<Entity>` for entity storage instead of Flecs.

### 2. Created Run Script (`run_integrated.sh`)

```bash
#!/bin/bash
set -e
cd "$(dirname "$0")"
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# ImGui objects
for f in vendor/imgui/build/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Jolt wrapper + objects
OBJ_ARGS="$OBJ_ARGS --obj vendor/jolt_wrapper.o"
for f in vendor/JoltPhysics/distr/objs/*.o; do
    OBJ_ARGS="$OBJ_ARGS --obj $f"
done

# Raylib dylib
OBJ_ARGS="$OBJ_ARGS --lib vendor/raylib/distr/libraylib_jank.dylib"

jank $OBJ_ARGS \
    -I./vendor/imgui \
    -I./vendor/raylib/src \
    -I./vendor/JoltPhysics \
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

### 1. Add 3D Rendering Support

Options:
- Rebuild raylib dylib with `rmodels` module included
- Implement basic 3D shapes using `rlgl` primitives (rlVertex3f, etc.)
- Use BeginMode3D/EndMode3D with custom vertex drawing

### 2. Integrate Flecs Properly

Options:
- Create a C++ wrapper library (like jolt_wrapper) that pre-compiles template instantiations
- Use Flecs C API directly in jank
- Compile entity management functions into object files

### 3. WASM Version

Create `run_integrated_wasm.sh` following the pattern from `run_imgui_wasm.sh`:
- Use `--native-lib` for JIT during AOT
- Use `--prelink-lib` for WASM object files
- Create HTML canvas wrapper

### 4. Add More Physics Features

- Collision callbacks
- Different body shapes (boxes, capsules)
- Constraints and joints
- Raycasting

## File Structure

```
src/
└── my_integrated_demo.jank  # Integrated demo (Raylib + ImGui + Jolt)

run_integrated.sh            # Native run script

vendor/
├── imgui/build/*.o          # ImGui object files
├── raylib/distr/            # Raylib dylib
├── JoltPhysics/distr/objs/  # Jolt object files
└── jolt_wrapper.o           # Jolt C wrapper
```
