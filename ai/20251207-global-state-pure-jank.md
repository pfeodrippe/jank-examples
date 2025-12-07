# Converting Global State Accessors to Pure jank

**Date**: 2025-12-07

## Summary

Converted all C++ global state accessors to pure jank using `cpp/box` and `cpp/new`.

## Before (C++)

```cpp
static float* g_time_scale = nullptr;
inline float& get_time_scale() {
    if (!g_time_scale) g_time_scale = new float(1.0f);
    return *g_time_scale;
}

// Similar for: get_spawn_count, get_view_scale, get_view_offset_x/y, get_font_tex
```

## After (jank)

```clojure
(defonce *time-scale (cpp/box (cpp/new cpp/float 1.0)))
(defonce *spawn-count (cpp/box (cpp/new cpp/int 5)))
(defonce *view-scale (cpp/box (cpp/new cpp/float 15.0)))
(defonce *view-offset-x (cpp/box (cpp/new cpp/float 640.0)))
(defonce *view-offset-y (cpp/box (cpp/new cpp/float 500.0)))
(defonce *font-tex (cpp/box (cpp/new (cpp/type "unsigned int") 0)))
```

## Key Patterns

### 1. Creating mutable C++ values for jank storage

```clojure
(defonce *my-var (cpp/box (cpp/new cpp/float initial-value)))
```

- `cpp/new cpp/float 1.0` - allocates float on heap, returns `float*`
- `cpp/box` - wraps the native pointer in an opaque_box for jank storage
- `defonce` - ensures single initialization

### 2. Reading values

```clojure
(cpp/* (cpp/unbox cpp/float* *my-var))
```

- `cpp/unbox cpp/float*` - extracts `float*` from opaque_box
- `cpp/*` - dereferences the pointer

### 3. Writing values

```clojure
(cpp/= (cpp/* (cpp/unbox cpp/float* *my-var)) (cpp/float. new-value))
```

- Same as reading, but wrapped in `cpp/=` assignment
- `(cpp/float. new-value)` converts jank number to native float

### 4. Passing to ImGui sliders (needs pointer)

```clojure
(imgui/SliderFloat "Label" (cpp/unbox cpp/float* *my-var) 0.1 3.0)
```

- ImGui sliders need the raw pointer for in-place modification
- Don't dereference with `cpp/*` here

## Type Name Consistency

**IMPORTANT**: The type used in `cpp/new` must match the type in `cpp/unbox`:

```clojure
;; WRONG - type names don't match!
(defonce *x (cpp/box (cpp/new cpp/uint 0)))
(cpp/unbox cpp/uint* *x)  ; Error: "unsigned int*" vs "uint*"

;; CORRECT - use consistent type names
(defonce *x (cpp/box (cpp/new (cpp/type "unsigned int") 0)))
(cpp/unbox (cpp/type "unsigned int*") *x)  ; Works!
```

- `cpp/uint` becomes `unsigned int*` when boxed
- `cpp/uint*` is interpreted as `uint*` (different type!)
- Use `(cpp/type "unsigned int")` and `(cpp/type "unsigned int*")` for consistency

## Cannot Use Helper Functions for Native Pointers

**This pattern DOES NOT work:**

```clojure
;; BAD - can't return native pointer from jank function
(defn my-var-ptr [] (cpp/unbox cpp/float* *my-var))
(cpp/* (my-var-ptr))  ; Error!
```

**Use inline pattern instead:**

```clojure
;; GOOD - inline the unbox at each use site
(cpp/* (cpp/unbox cpp/float* *my-var))
```

Native pointers cannot be returned from jank functions. Always inline the `cpp/unbox` call at each usage site.

## Files Changed

- `src/my_integrated_demo.jank`:
  - Added `*time-scale`, `*spawn-count`, `*view-scale`, `*view-offset-x`, `*view-offset-y`, `*font-tex` defonce definitions
  - Removed C++ globals: `g_time_scale`, `g_spawn_count`, `g_view_scale`, etc.
  - Removed C++ accessor functions: `get_time_scale()`, `set_time_scale()`, etc.
  - Updated all usages to inline `cpp/unbox` pattern

## Functions Updated

- `time-scale`, `spawn-count`, `view-scale` - accessor functions
- `scale-view!`, `add-view-offset!`, `set-view-offset!` - mutator functions
- `imgui-load-font-texture!` - font-tex storage
- `imgui-shutdown!` - font-tex cleanup
- `imgui-reset-view!` - reset view state
- `physics-to-screen` - view transform calculation
- `draw-entity!` - entity rendering
- `draw-imgui-panel!` - ImGui sliders and display

## Performance Optimization: Per-Frame Caching

The `cpp/unbox` calls have overhead. In hot paths (called 200+ times per frame), cache values in jank atoms:

```clojure
;; Per-frame cached view values
(def ^:private *cached-view-offset-x (atom 640.0))
(def ^:private *cached-view-offset-y (atom 500.0))
(def ^:private *cached-view-scale (atom 15.0))

(defn cache-view-values!
  "Cache view values at start of frame to avoid repeated cpp/unbox calls."
  []
  (reset! *cached-view-offset-x (double (cpp/* (cpp/unbox cpp/float* *view-offset-x))))
  (reset! *cached-view-offset-y (double (cpp/* (cpp/unbox cpp/float* *view-offset-y))))
  (reset! *cached-view-scale (double (cpp/* (cpp/unbox cpp/float* *view-scale)))))
```

Call `cache-view-values!` once at start of each frame, then use `@*cached-view-scale` etc. in hot paths.

**Before**: 800+ `cpp/unbox` calls per frame → 2 FPS
**After**: 3 `cpp/unbox` calls per frame → 60 FPS

## C++ Code Removed

All global state accessors removed from `cpp/raw`:
- `g_time_scale`, `get_time_scale()`, `set_time_scale()`
- `g_spawn_count`, `get_spawn_count()`, `set_spawn_count()`
- `g_view_scale`, `get_view_scale()`, `set_view_scale()`
- `g_view_offset_x`, `get_view_offset_x()`, `set_view_offset_x()`
- `g_view_offset_y`, `get_view_offset_y()`, `set_view_offset_y()`
- `g_font_tex`, `get_font_tex()`, `set_font_tex()`
