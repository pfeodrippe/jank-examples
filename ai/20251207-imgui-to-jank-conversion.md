# ImGui C++ to jank Conversion

**Date**: 2025-12-07

## Summary

Converted ImGui initialization and input handling from C++ to pure jank.

## Functions Converted

### 1. `imgui_update_input` -> `imgui-update-input!`

**Before (C++):**
```cpp
inline void imgui_update_input() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)GetScreenWidth(), (float)GetScreenHeight());
    io.DeltaTime = GetFrameTime();
    Vector2 m = GetMousePosition();
    io.AddMousePosEvent(m.x, m.y);
    io.AddMouseButtonEvent(0, IsMouseButtonDown(MOUSE_LEFT_BUTTON));
    io.AddMouseButtonEvent(1, IsMouseButtonDown(MOUSE_RIGHT_BUTTON));
    Vector2 wh = GetMouseWheelMoveV();
    io.AddMouseWheelEvent(wh.x, wh.y);
    io.AddKeyEvent(ImGuiMod_Ctrl, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    int c = GetCharPressed();
    while (c > 0) { io.AddInputCharacter(c); c = GetCharPressed(); }
}
```

**After (jank):**
```clojure
(defn imgui-update-input!
  []
  (let* [io (imgui/GetIO)
         _ (cpp/= (cpp/.-DisplaySize io)
                  (imgui-h/ImVec2. (cpp/float. (rl/GetScreenWidth))
                                   (cpp/float. (rl/GetScreenHeight))))
         _ (cpp/= (cpp/.-DeltaTime io) (rl/GetFrameTime))
         m (rl/GetMousePosition)
         _ (cpp/.AddMousePosEvent io (cpp/.-x m) (cpp/.-y m))
         ;; ... rest of input handling
         ]
    ;; while loop -> loop/recur
    (loop [c (rl/GetCharPressed)]
      (when (> c 0)
        (cpp/.AddInputCharacter io c)
        (recur (rl/GetCharPressed)))))
  nil)
```

### 2. `imgui_init` -> `imgui-init!`

Converted most of init to jank, but kept a C++ helper `imgui_load_font_texture` for the complex out-parameter font loading.

**jank version:**
```clojure
(defn imgui-init!
  []
  (imgui/CreateContext)
  (let* [io (imgui/GetIO)
         _ (cpp/= (cpp/.-ConfigFlags io)
                  (bit-or (cpp/.-ConfigFlags io)
                          imgui-h/ImGuiConfigFlags_NavEnableKeyboard))]
    (imgui/StyleColorsDark)
    (cpp/= (cpp/.-DisplaySize io)
           (imgui-h/ImVec2. (cpp/float. (rl/GetScreenWidth))
                            (cpp/float. (rl/GetScreenHeight))))
    (cpp/imgui_load_font_texture))
  nil)
```

## Key Patterns Used

### 1. Struct field assignment
```clojure
;; io.DisplaySize = ImVec2(...)
(cpp/= (cpp/.-DisplaySize io) (imgui-h/ImVec2. ...))
```

### 2. Method calls on references
```clojure
;; io.AddMousePosEvent(x, y)
(cpp/.AddMousePosEvent io x y)
```

### 3. Bitwise OR assignment
```clojure
;; io.ConfigFlags |= FLAG
(cpp/= (cpp/.-ConfigFlags io)
       (bit-or (cpp/.-ConfigFlags io) FLAG))
```

### 4. while loop -> loop/recur
```clojure
;; while (c > 0) { ... c = GetNext(); }
(loop [c (rl/GetCharPressed)]
  (when (> c 0)
    (do-something c)
    (recur (rl/GetCharPressed))))
```

### 5. Header require scopes
- `imgui` - with `ImGui::` scope for functions
- `imgui-h` - global scope for constants like `ImGuiMod_Ctrl`, `ImGuiConfigFlags_NavEnableKeyboard`

## What Stays in C++

The font texture loading (`imgui_load_font_texture`) stays in C++ because:
- Uses out parameters: `GetTexDataAsRGBA32(&pixels, &w, &h)`
- Creates struct with initializer list: `Image img = { pixels, w, h, 1, FORMAT }`
- Complex pointer casting for texture ID

## Files Changed

- `src/my_integrated_demo.jank`:
  - Added `imgui-update-input!` (pure jank)
  - Added `imgui-init!` (mostly jank, calls C++ helper)
  - Added `imgui_load_font_texture` C++ helper
  - Removed old `imgui_init` and `imgui_update_input` C++ functions

## Lessons Learned

1. **`bit-or` returns jank object** - must wrap with `(cpp/int ...)` for C++ assignment:
   ```clojure
   (cpp/= (cpp/.-ConfigFlags io)
          (cpp/int (bit-or (cpp/.-ConfigFlags io) FLAG)))
   ```

2. **`loop/recur` doesn't preserve native types** - for while loops with native ints, use `cpp/raw`:
   ```clojure
   ;; Instead of loop/recur:
   (cpp/raw "{ int c = GetCharPressed(); while (c > 0) { io.AddInputCharacter(c); c = GetCharPressed(); } }")
   ```

3. **Void-returning functions in `let*`** - bind to `_`:
   ```clojure
   (let* [_ (imgui/CreateContext)
          _ (imgui/StyleColorsDark)]
     ...)
   ```

## Test Result

**SUCCESS** - `./run_integrated.sh` runs correctly with ImGui panel rendering properly.
