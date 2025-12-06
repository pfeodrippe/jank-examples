# Pure jank imgui-shutdown! and imgui-wants-input?

**Date**: 2025-12-05

## What Was Learned

### 1. Converting imgui_shutdown to pure jank

Successfully converted the C++ `imgui_shutdown` function to pure jank:

**Before (C++ wrapper):**
```cpp
inline void imgui_shutdown() {
    if (get_font_tex()) { rlUnloadTexture(get_font_tex()); get_font_tex() = 0; }
    ImGui::DestroyContext();
}
```

**After (Pure jank):**
```clojure
(defn imgui-shutdown!
  "Shutdown ImGui. Pure jank using rlgl header require."
  []
  (let [tex (cpp/get_font_tex)]
    (when (not= tex 0)
      (rlgl/rlUnloadTexture tex)
      (cpp/= (cpp/get_font_tex) (cpp/uint. 0))))
  (imgui/DestroyContext))
```

Key points:
- Added `["rlgl.h" :as rlgl :scope ""]` header require for `rlUnloadTexture`
- Used `cpp/=` for assigning to C++ reference return value
- Used `cpp/uint.` for type conversion to unsigned int

### 2. Converting imgui_wants_input to pure jank

**Before (C++ wrapper):**
```cpp
inline bool imgui_wants_input() {
    return ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
}
```

**After (Pure jank):**
```clojure
(defn imgui-wants-input?
  "Check if ImGui wants mouse/keyboard input. Pure jank via header require."
  []
  (let [io (imgui/GetIO)]
    (if (cpp/.-WantCaptureMouse io)
      true
      (cpp/.-WantCaptureKeyboard io))))
```

**Important**: Using `or` with raw C++ bool values causes a C++ compilation error:
```
error: non-const lvalue reference to type 'bool' cannot bind to an initializer list temporary
```

The workaround is to use `if/else` instead of `or` for C++ boolean values.

### 3. Function definition order matters

When moving functions around in jank, remember that functions must be defined **before** they are called. In this case, `imgui-wants-input?` had to be defined before `handle-input!` which calls it.

## Changes Made

### 1. Added rlgl header require
**File**: `src/my_integrated_demo.jank`

```clojure
;; Added to ns requires:
["rlgl.h" :as rlgl :scope ""]
```

### 2. Converted ImGui functions to pure jank
**File**: `src/my_integrated_demo.jank`

- `imgui-shutdown!` - Now uses `rlgl/rlUnloadTexture` and `imgui/DestroyContext`
- `imgui-wants-input?` - Now uses `imgui/GetIO` and field access

### 3. Removed C++ functions from cpp/raw
**File**: `src/my_integrated_demo.jank`

Replaced with comments:
```cpp
// imgui_wants_input now in pure jank!
// imgui_shutdown now in pure jank!
```

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## Key Lessons

1. **rlgl.h is separate from raylib.h** - `rlUnloadTexture` is in `rlgl.h`, not `raylib.h`. Need a separate header require.

2. **`or` doesn't work with C++ bools** - Use `if/else` instead:
   ```clojure
   ;; BAD - causes C++ compile error:
   (or (cpp/.-WantCaptureMouse io) (cpp/.-WantCaptureKeyboard io))

   ;; GOOD - use if/else:
   (if (cpp/.-WantCaptureMouse io)
     true
     (cpp/.-WantCaptureKeyboard io))
   ```

3. **Use `let` not `let*`** - The user prefers standard `let` over `let*` for native value bindings.

4. **Function ordering matters** - Define functions before they're called in jank.

## Summary of New Header Require

| Header | Alias | Scope | Functions |
|--------|-------|-------|-----------|
| `rlgl.h` | `rlgl` | `""` | `rlUnloadTexture`, `rlDrawRenderBatchActive`, etc. |

## What's Still in cpp/raw (ImGui)

These still need C++ wrappers due to complexity:
- `imgui_init` - Complex setup with fonts, pixel data
- `imgui_update_input` - While loop for GetCharPressed
- `imgui_render_draw_data` - Nested loops with pointer arithmetic

## What's Next

- Consider converting `imgui_init` if feasible
- Look at other complex ImGui rendering code
- Keep `imgui_render_draw_data` in C++ (too many nested loops)
