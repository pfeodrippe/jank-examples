# Learning: Pure jank ImGui Panel with #cpp Reader Macro

**Date**: 2024-12-02

## What I Learned

### 1. The `#cpp` Reader Macro for Variadic C Functions

C variadic functions like `ImGui::Text`, `ImGui::BulletText`, `printf` work directly from jank using `#cpp` for string literals:

```clojure
;; Format string with arguments - works!
(imgui/Text #cpp "FPS: %d" (rl/GetFPS))
(imgui/Text #cpp "Jolt Bodies: %d" (cpp/jolt_num_bodies w))
(imgui/Text #cpp "View: (%.0f, %.0f) scale: %.1f" x y scale)

;; Simple strings
(imgui/BulletText #cpp "Drag to pan")
(imgui/Button #cpp "Reset Simulation")
```

### 2. Non-Variadic ImGui Functions Work Directly

Simple ImGui calls need no helpers at all:

```clojure
(imgui/Begin #cpp "Physics Debug")  ;; needs #cpp for string
(imgui/Separator)                    ;; no args
(imgui/SameLine)                     ;; no args
(imgui/End)                          ;; no args
```

### 3. Only Mutable Reference Widgets Need cpp Helpers

ImGui widgets that modify values via pointer still need C++ helpers:

```cpp
// These need C++ because they take mutable references
inline bool imgui_checkbox_paused() {
    return ImGui::Checkbox("Paused", &get_paused());
}
inline bool imgui_slider_time_scale() {
    return ImGui::SliderFloat("Time Scale", &get_time_scale(), 0.1f, 3.0f);
}
```

### 4. Function Definition Order Matters in jank

Unlike Clojure, jank requires functions to be defined before use:

```clojure
;; ERROR: num-bodies not defined yet
(defn native-draw-imgui-panel [w]
  (imgui/Text #cpp "Bodies: %d" (num-bodies w)))

(defn num-bodies [w] ...)  ;; defined too late!

;; FIX: Use cpp function directly
(defn native-draw-imgui-panel [w]
  (imgui/Text #cpp "Bodies: %d" (cpp/jolt_num_bodies w)))
```

## Final Pure jank ImGui Panel

```clojure
(defn native-draw-imgui-panel [w]
  ;; Begin panel
  (imgui/Begin #cpp "Physics Debug")

  ;; Stats section - all pure jank!
  (imgui/Text #cpp "FPS: %d" (rl/GetFPS))
  (imgui/Separator)
  (imgui/Text #cpp "Jolt Bodies: %d" (cpp/jolt_num_bodies w))
  (imgui/Text #cpp "Active: %d" (cpp/jolt_num_active w))
  (imgui/Text #cpp "Flecs Entities: %d" (cpp/flecs_entity_count))
  (imgui/Separator)

  ;; Controls (mutable refs need cpp helpers)
  (cpp/imgui_checkbox_paused)
  (cpp/imgui_slider_time_scale)
  (cpp/imgui_slider_spawn_count)
  (imgui/Separator)

  ;; Buttons - pure jank with when!
  (when (imgui/Button #cpp "Reset Simulation")
    (cpp/imgui_set_reset_requested))
  (imgui/SameLine)
  (when (imgui/Button #cpp "Reset View")
    (cpp/imgui_reset_view))

  ;; View info - pure jank!
  (imgui/Text #cpp "View: (%.0f, %.0f) scale: %.1f"
              (cpp/native_get_view_offset_x)
              (cpp/native_get_view_offset_y)
              (cpp/native_get_view_scale))
  (imgui/Separator)

  ;; Help text - pure jank!
  (imgui/Text #cpp "Controls:")
  (imgui/BulletText #cpp "Drag to pan")
  (imgui/BulletText #cpp "Scroll to zoom")
  (imgui/BulletText #cpp "Space to spawn")

  ;; End panel
  (imgui/End))
```

## Commands I Ran

```bash
./run_integrated.sh 2>&1 &
sleep 8
```

## Files Modified

1. **`src/my_integrated_demo.jank`**:
   - Converted `native-draw-imgui-panel` to pure jank with `#cpp` macros
   - Removed ~25 lines of C++ helper functions
   - Only 5 cpp helpers remain (for mutable refs and state mutation)

## What Got Removed (C++)

- `imgui_text_fps`, `imgui_text_jolt_stats`, `imgui_text_flecs_stats`, `imgui_text_view_stats`
- `imgui_text_controls`, `imgui_bullet_pan`, `imgui_bullet_zoom`, `imgui_bullet_spawn`
- `imgui_begin_physics_debug`, `imgui_same_line`, `imgui_end`
- `imgui_button_reset_sim`, `imgui_button_reset_view`

## What Remains (Still Needed)

Only 5 cpp helpers that **require** C++ (mutable references or state mutation):

| Helper | Why C++ Required |
|--------|------------------|
| `imgui_checkbox_paused` | Needs `&get_paused()` mutable ref |
| `imgui_slider_time_scale` | Needs `&get_time_scale()` mutable ref |
| `imgui_slider_spawn_count` | Needs `&get_spawn_count()` mutable ref |
| `imgui_set_reset_requested` | Mutates global state |
| `imgui_reset_view` | Mutates multiple globals |

## Key Insight

**The `#cpp` reader macro is the key to eliminating cpp helpers for text/variadic functions.**

Before: Every ImGui text call needed a C++ wrapper
After: Direct `(imgui/Text #cpp "format" args...)` calls

## What's Next

1. Apply same pattern to other demos
2. Consider jank atoms/refs for state to eliminate remaining cpp helpers
3. Update native resources guide with `#cpp` examples
