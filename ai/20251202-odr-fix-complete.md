# ODR Issue Fix for Static Variables in jank JIT

**Date**: 2024-12-02

## What I Learned

### Critical: Static Variables in Inline Functions Have ODR Issues with JIT

When jank JIT-compiles inline C++ functions, static variables inside them get **separate instances** for each function that uses them. This is the One Definition Rule (ODR) violation in action.

#### The Problem

```cpp
// BAD: Each inline function gets its own copy!
static float g_time_scale = 1.0f;

inline void set_time_scale(double v) { g_time_scale = (float)v; }  // Uses copy A
inline double get_time_scale() { return g_time_scale; }            // Uses copy B
inline void draw_panel() { ImGui::SliderFloat("Scale", &g_time_scale, ...); }  // Uses copy C
```

When these functions are compiled separately by the JIT, each gets its own `g_time_scale` instance. Changes in one don't affect the others!

#### The Solution: Heap-Allocated Pointers with Getter Functions

```cpp
// GOOD: Single shared instance through pointer
static float* g_time_scale_ptr = nullptr;

inline float& get_time_scale() {
    if (!g_time_scale_ptr) g_time_scale_ptr = new float(1.0f);
    return *g_time_scale_ptr;
}

inline void set_time_scale(double v) { get_time_scale() = (float)v; }
inline double native_get_time_scale() { return get_time_scale(); }
inline void draw_panel() { ImGui::SliderFloat("Scale", &get_time_scale(), ...); }
```

The key insight: the **pointer itself** (`g_time_scale_ptr`) may have multiple copies, but they all get initialized to `nullptr` and the first one to run allocates the heap memory. Subsequent calls find the pointer non-null and use the same heap location.

### All Static Variables Need This Pattern

In the integrated demo, I had to convert ALL static variables:
- `g_entities` → `g_entities_ptr` + `get_entities()`
- `g_paused` → `g_paused_ptr` + `get_paused()`
- `g_time_scale` → `g_time_scale_ptr` + `get_time_scale()`
- `g_spawn_count` → `g_spawn_count_ptr` + `get_spawn_count()`
- `g_view_scale` → `g_view_scale_ptr` + `get_view_scale()`
- `g_view_offset_x` → `g_view_offset_x_ptr` + `get_view_offset_x()`
- `g_view_offset_y` → `g_view_offset_y_ptr` + `get_view_offset_y()`
- `g_font_tex` → `g_font_tex_ptr` + `get_font_tex()`

## What I Did

### 1. Converted All Static Variables to Heap-Pointer Pattern

Every static variable in the C++ layer now uses:
```cpp
static T* g_var_ptr = nullptr;
inline T& get_var() {
    if (!g_var_ptr) g_var_ptr = new T(default_value);
    return *g_var_ptr;
}
```

### 2. Added Reset Simulation Feature

Added a "Reset Simulation" button that:
- Destroys the old Jolt physics world
- Clears all entities
- Creates a new world with floor
- Respawns initial balls

This required restructuring the main loop to pass `world` as a loop parameter:
```clojure
(loop [world initial-world]
  (if (raylib-should-close?)
    (cleanup...)
    (do
      (process-frame...)
      (if (reset-requested?)
        (recur (reset-simulation! world))
        (recur world)))))
```

### 3. Fixed Physics Movement

After applying the heap-pointer pattern to all variables, the physics now works correctly:
- Balls fall with gravity
- Time Scale slider affects simulation speed
- Pause checkbox stops physics
- Pan/zoom controls work
- Reset View button resets camera position

## Commands Reference

```bash
# Run integrated demo
./run_integrated.sh

# Features:
# - Drag to pan the view
# - Scroll to zoom in/out
# - Space to spawn new balls
# - ImGui panel with: FPS, body count, pause, time scale, spawn count
# - Reset Simulation button - recreates physics world and balls
# - Reset View button - resets camera position and zoom
```

## Key Pattern for jank C++ Integration

When using static variables in `cpp/raw` code that will be used across multiple inline functions:

1. **Never use plain static variables** - they get duplicated per JIT compilation unit
2. **Always use heap-allocated pointers** with getter functions
3. **All code accesses through the getter** - both reads and writes
4. **ImGui widgets work** because they take reference to the getter: `&get_var()`

## File Structure

```
src/
└── my_integrated_demo.jank  # Integrated demo with ODR-safe static variables

ai/
├── 20251202-integrated-demo.md        # Initial demo documentation
├── 20251202-integrated-demo-static.md # Static linking documentation
└── 20251202-odr-fix-complete.md       # This file - ODR fix documentation
```
