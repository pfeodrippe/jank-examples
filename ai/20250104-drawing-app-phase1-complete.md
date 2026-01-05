# Drawing App Phase 1 - Complete

## Date: 2025-01-04

## Summary
Successfully implemented Phase 1 of the Looom-inspired drawing app on macOS. The app can now render strokes via SDL_Renderer using Metal backend.

## What I Learned

### 1. C++ Namespace Conflicts with jank
When using header requires with `:scope "namespace"`, the namespace name can conflict with jank's generated namespace. For example:
- jank namespace: `vybe.app.drawing` -> C++ namespace `vybe::app::drawing`
- Header namespace: `drawing` -> When calling `drawing::func()`, C++ finds `vybe::app::drawing` first!

**Solution**: Use a short, unique namespace name like `dc` instead of `drawing`:
```clojure
["vybe/app/drawing/native/drawing_canvas.hpp" :as canvas :scope "dc"]
```

### 2. Java-isms Don't Work in jank
jank doesn't have Java's standard library, so:
- `Math/sqrt` -> Use `cmath/sqrt` with `["cmath" :as cmath :scope ""]`
- `System/currentTimeMillis` -> Use a C++ function like `SDL_GetTicks()`

### 3. MoltenVK SPIR-V Issues
Embedded SPIR-V shaders in C++ headers can have compatibility issues with MoltenVK's SPIRV-Cross converter.

**Solution**: Use SDL_Renderer instead of raw Vulkan for 2D rendering. SDL_Renderer automatically uses Metal on macOS/iOS and handles shaders internally.

### 4. SDL_Renderer Triangle Rendering
SDL3's `SDL_RenderGeometry()` can render triangles with per-vertex colors - perfect for bezier stroke tessellation.

```cpp
std::vector<SDL_Vertex> sdl_verts;
for (const auto& v : vertices) {
    SDL_Vertex sv;
    sv.position.x = v.x;
    sv.position.y = v.y;
    sv.color.r = v.r; sv.color.g = v.g; sv.color.b = v.b; sv.color.a = v.a;
    sdl_verts.push_back(sv);
}
SDL_RenderGeometry(renderer, nullptr, sdl_verts.data(), sdl_verts.size(), nullptr, 0);
```

### 5. jank Atoms Cannot Hold nil
jank atoms throw an assertion error when you try to `reset!` them to nil:
```
Assertion failed! o.is_some()
```

**Solution**: Use an empty map `{}` as a sentinel value instead of nil:
```clojure
(defonce *current-stroke (atom {}))  ;; Not (atom nil)
(reset! *current-stroke {})          ;; Not (reset! *current-stroke nil)
(if (empty? current) ...)            ;; Check with empty? not nil?
```

### 6. Lazy Sequences Can Cause Crashes
jank's handling of lazy sequences from `for` can cause crashes when passed to `vec` or other functions.

**Solution**: Use explicit `loop/recur` instead of `for` for more reliable iteration.

### 7. Name Collisions with clojure.core
When defining functions like `run!` in your namespace, they shadow `clojure.core/run!`. This causes a warning.

**Solution**: Use unique names like `run-app!` to avoid collisions.

## Commands Used

```bash
# Run the drawing app
make drawing

# Build output is saved for debugging
make drawing 2>&1 | tee /tmp/drawing_build.txt
```

## Files Created/Modified

### New Files
- `src/vybe/app/drawing/native/drawing_canvas.hpp` - SDL_Renderer based 2D canvas (~400 lines)
- `src/vybe/app/drawing/core.jank` - Data structures
- `src/vybe/app/drawing/state.jank` - Atom-based state management
- `src/vybe/app/drawing/math.jank` - Bezier tessellation (ALL JANK!)
- `src/vybe/app/drawing/input.jank` - Touch/mouse event processing
- `src/vybe/app/drawing.jank` - Main entry point
- `bin/run_drawing.sh` - Run script

### Architecture
- **99% jank**: All data structures, math, state management, input processing
- **~1% C++**: Only SDL window/renderer/events (what can't be done in jank)

## What's Next

1. **Test on iOS Simulator** - The app should work with touch input
2. **Add Animation Timeline** - Looom-style threads with frame-per-thread
3. **Playback Controls** - Play/pause/scrub animations
4. **Export** - SVG or video export

## Current Status
- macOS: Working! Window opens, strokes can be drawn
- iOS: Pending test
