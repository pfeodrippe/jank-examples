# Learning: cpp/& for Mutable References - Issues Found

**Date**: 2024-12-02

## What I Learned

### 1. cpp/& Works for Checkbox but NOT for Sliders

Tested `cpp/&` for ImGui widgets that take mutable pointers:

```clojure
;; WORKS: Checkbox with cpp/&
(imgui/Checkbox "Paused" (cpp/& (cpp/get_paused)))

;; DOES NOT WORK: Sliders with cpp/& cause JIT segfault
(imgui/SliderFloat "Time Scale" (cpp/& (cpp/get_time_scale)) 0.1 3.0)  ;; JIT crash
(imgui/SliderInt "Spawn Count" (cpp/& (cpp/get_spawn_count)) 1 20)     ;; JIT crash
```

**Summary:**
- `Checkbox` with `cpp/&` → **Works**
- `SliderFloat` / `SliderInt` with `cpp/&` → **JIT segfault during compilation**

### 2. Working Pattern: Mixed Approach

Use `cpp/&` for Checkbox (works), use C++ helpers for Sliders (required):

```clojure
;; Checkbox - cpp/& works!
(imgui/Checkbox "Paused" (cpp/& (cpp/get_paused)))

;; Sliders - need C++ helpers
(cpp/imgui_slider_time_scale)
(cpp/imgui_slider_spawn_count)
```

C++ slider helpers in `cpp/raw`:
```cpp
inline bool imgui_slider_time_scale() {
    return ImGui::SliderFloat(\"Time Scale\", &get_time_scale(), 0.1f, 3.0f);
}
inline bool imgui_slider_spawn_count() {
    return ImGui::SliderInt(\"Spawn Count\", &get_spawn_count(), 1, 20);
}
```

### 3. What DOES Work in Pure jank

These ImGui patterns work without C++ helpers:

```clojure
;; Panel structure - no helpers needed
(imgui/Begin #cpp "Panel Title")
(imgui/End)
(imgui/Separator)
(imgui/SameLine)

;; Text with variadic args - needs #cpp reader macro
(imgui/Text #cpp "FPS: %d" (rl/GetFPS))
(imgui/Text #cpp "Value: %.1f" some-float)
(imgui/BulletText #cpp "Some text")

;; Buttons - return bool, work in when
(when (imgui/Button #cpp "Click Me")
  (do-something))
```

### 4. Escape Quotes in cpp/raw

When adding C++ code inside `cpp/raw`, you must escape quotes:

```clojure
;; WRONG - causes parse error
(cpp/raw "
inline void foo() {
    ImGui::Text(\"Hello\");  // <-- need escaped quotes
}
")

;; CORRECT
inline void foo() {
    ImGui::Text(\\\"Hello\\\");
}
```

## Commands I Ran

```bash
./run_integrated.sh 2>&1 | head -80
```

## Files Modified

1. **`src/my_integrated_demo.jank`**:
   - Attempted `cpp/&` pattern → caused segfaults
   - Reverted to C++ helpers for Checkbox/Slider widgets
   - Demo runs correctly with C++ helpers

## Key Insight

**`cpp/&` has issues with ImGui mutable ref widgets.** Stick to C++ helpers for:
- `ImGui::Checkbox` (takes `bool*`)
- `ImGui::SliderFloat` (takes `float*`)
- `ImGui::SliderInt` (takes `int*`)
- Any widget that modifies a value via pointer

## Current Working ImGui Panel

```clojure
(defn native-draw-imgui-panel [w]
  (imgui/Begin #cpp "Physics Debug")

  ;; Stats - pure jank with #cpp for variadic Text
  (imgui/Text #cpp "FPS: %d" (rl/GetFPS))
  (imgui/Separator)
  (imgui/Text #cpp "Jolt Bodies: %d" (cpp/jolt_num_bodies w))
  (imgui/Text #cpp "Active: %d" (cpp/jolt_num_active w))
  (imgui/Text #cpp "Flecs Entities: %d" (cpp/flecs_entity_count))
  (imgui/Separator)

  ;; Controls - need cpp helpers for mutable refs
  (cpp/imgui_checkbox_paused)
  (cpp/imgui_slider_time_scale)
  (cpp/imgui_slider_spawn_count)
  (imgui/Separator)

  ;; Buttons - pure jank!
  (when (imgui/Button #cpp "Reset Simulation")
    (cpp/imgui_set_reset_requested))
  (imgui/SameLine)
  (when (imgui/Button #cpp "Reset View")
    (cpp/imgui_reset_view))

  ;; View info
  (imgui/Text #cpp "View: (%.0f, %.0f) scale: %.1f"
              (cpp/native_get_view_offset_x)
              (cpp/native_get_view_offset_y)
              (cpp/native_get_view_scale))
  (imgui/Separator)

  ;; Help text
  (imgui/Text #cpp "Controls:")
  (imgui/BulletText #cpp "Drag to pan")
  (imgui/BulletText #cpp "Scroll to zoom")
  (imgui/BulletText #cpp "Space to spawn")

  (imgui/End))
```

## What's Next

1. Report `cpp/&` issue to jank developers
2. Document the C++ helper pattern in native resources guide
3. Consider if there's a different approach for mutable refs
