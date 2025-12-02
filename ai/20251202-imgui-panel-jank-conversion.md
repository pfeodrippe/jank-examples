# Learning: Converting ImGui Panel to jank

**Date**: 2024-12-02

## What I Learned

### 1. opaque_box Pointer Extraction

To extract `void*` from an opaque_box in jank, you need a minimal cpp helper:

```cpp
inline void* opaque_box_ptr(jank::runtime::object_ref box) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(box);
    return o->data.data;
}
```

Then use from jank:
```clojure
(cpp/opaque_box_ptr my-boxed-value)
```

**Why**: `cpp/.-field` works on compile-time types, but `object_ref` is dynamically typed - need `expect_object` cast.

### 2. void Functions in let* Cause Errors

jank's `let*` tries to wrap the last expression in `jtl::option<T>`. If T is `void`, you get:
```
error: cannot form a reference to 'void'
```

**Fix**: Make cpp helpers return `jank::runtime::object_ref` instead of `void`:
```cpp
inline jank::runtime::object_ref imgui_end() {
    ImGui::End();
    return jank::runtime::jank_nil;
}
```

### 3. Variadic C Functions Don't Work from jank

Functions like `ImGui::Text(format, ...)` and `ImGui::BulletText(format, ...)` can't be called via header require. Create helpers:

```cpp
inline void imgui_text_fps() {
    ImGui::Text("FPS: %d", GetFPS());
}
```

### 4. Mutable Reference Widgets Need cpp Helpers

ImGui widgets that modify values via pointer (Checkbox, Slider, etc.) need cpp:

```cpp
inline bool imgui_checkbox_paused() {
    return ImGui::Checkbox("Paused", &get_paused());
}
```

### 5. Pattern: jank Controls Flow, cpp Handles ImGui

```clojure
(defn native-draw-imgui-panel [w]
  (let* [world-ptr (cpp/opaque_box_ptr w)]
    (cpp/imgui_begin_physics_debug)
    ;; jank controls the logic!
    (when (cpp/imgui_button_reset_sim)
      (cpp/imgui_set_reset_requested))
    (cpp/imgui_end)))
```

## Commands I Ran

```bash
# Test integrated demo
./run_integrated.sh 2>&1 &
sleep 8
```

## Files Modified

1. **`src/my_integrated_demo.jank`**:
   - Added `opaque_box_ptr` helper for extracting void* from opaque_box
   - Added ImGui helper functions (imgui_begin_physics_debug, imgui_separator, etc.)
   - Made `imgui_end` return `jank_nil` instead of void
   - Converted `native-draw-imgui-panel` to pure jank with cpp helpers

## Key Insight

**The pattern for jank + ImGui:**
1. Simple ImGui calls (Begin, Separator, End) -> cpp helpers returning nil
2. Variadic text calls (Text, BulletText) -> cpp helpers with hardcoded strings
3. Mutable widgets (Checkbox, Slider) -> cpp helpers with internal state
4. Button logic -> jank `when` with cpp helper returning bool
5. State mutation -> cpp helpers that modify global state

## What's Next

1. Apply same pattern to other complex UI functions
2. Consider creating a jank ImGui DSL macro that generates cpp helpers
3. Test WASM build with the converted code
4. Document the void->nil pattern in claude.md
