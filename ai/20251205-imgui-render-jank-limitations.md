# Why imgui_render_draw_data Cannot Be Converted to Pure jank

**Date**: 2025-12-05

## Summary

After extensive attempts to convert `imgui_render_draw_data` from C++ to pure jank, we discovered fundamental limitations in jank's handling of native C++ types within closures. The function remains in `cpp/raw`.

## The Function

The `imgui_render_draw_data` function renders ImGui draw data with:
- 4 nested loops (cmd lists → draw commands → vertices → indices)
- Pointer arithmetic for vertex and index buffers
- Function pointer callbacks (ImDrawCallback)
- Multiple native types: `ImDrawVert*`, `ImDrawIdx*`, `ImDrawCmd*`

```cpp
inline void imgui_render_draw_data() {
    ImDrawData* dd = ImGui::GetDrawData();
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    for (int n = 0; n < dd->CmdListsCount; n++) {
        ImDrawList* cmdList = dd->CmdLists[n];
        ImDrawVert* vtxBuffer = cmdList->VtxBuffer.Data;
        ImDrawIdx* idxBuffer = cmdList->IdxBuffer.Data;

        for (int i = 0; i < cmdList->CmdBuffer.Size; i++) {
            ImDrawCmd* pc = &cmdList->CmdBuffer.Data[i];
            // ... render triangles
        }
    }
}
```

## Limitation 1: Native Types Cannot Be Captured in Closures

jank closures (created by `doseq`, `dotimes`, `for`, etc.) cannot capture native C++ types.

**Attempt:**
```clojure
(let* [vtx-buffer cpp/ImDrawVert* (cpp/.-Data (cpp/.-VtxBuffer cmd-list))]
  (doseq [i (range elem-count)]  ;; Creates a closure!
    (let* [vb cpp/ImDrawVert* vtx-buffer]  ;; ERROR: Can't capture native type
      ...)))
```

**Error:**
```
Unknown implicit conversion from ImDrawVert *& to jank::runtime::object *
```

**Why it happens:** When jank compiles `doseq`, it creates a closure object. Closures can only capture jank `object*` types, not raw C++ pointers.

## Limitation 2: Boxing/Unboxing Complexity

We tried boxing pointers before the closure:
```clojure
(let [vtx-box (cpp/box vtx-buffer)]
  (doseq [i (range count)]
    (let* [vtx cpp/ImDrawVert* (cpp/unbox cpp/ImDrawVert* vtx-box)]
      ...)))
```

This works in principle, but with 4 nested loops (cmd lists → commands → element count × 3), the boxing/unboxing overhead becomes:
1. Complex to write correctly
2. Potentially slow (boxing/unboxing on every inner iteration)
3. Error-prone (type mismatches at multiple levels)

## Limitation 3: Function Pointer Null Checks

ImGui draw commands can have user callbacks. In C++:
```cpp
if (pc->UserCallback != nullptr) { ... }
```

In jank, you cannot compare function pointers with `nullptr`:
```clojure
;; ERROR: Binary operator == is not supported for 'ImDrawCallback &' and 'nullptr_t'
(when-not (cpp/== (cpp/.-UserCallback pc) nullptr) ...)
```

**Workaround:** Required a C++ helper:
```cpp
inline bool imgui_cmd_has_no_callback(ImDrawCmd* pc) {
    return pc->UserCallback == nullptr;
}
```

## Limitation 4: Pointer Array Indexing

C++ has: `dd->CmdLists[n]` (array indexing on pointer)

jank has no native array indexing syntax for pointer arrays. Required C++ helper:
```cpp
inline ImDrawList* imgui_get_cmd_list(ImDrawData* dd, int n) {
    return dd->CmdLists[n];
}
```

## What Would Be Needed for Pure jank

1. **Native closure capture** - Allow closures to capture and use native C++ types
2. **Array indexing operator** - `(cpp/[] ptr index)` for pointer array access
3. **Function pointer comparison** - `(cpp/== fn-ptr nullptr)` support
4. **Efficient loop primitives** - C-style for loops without closure overhead

## Current Solution

Keep `imgui_render_draw_data` in `cpp/raw`. The jank wrapper is minimal:

```clojure
(defn imgui-draw!
  "Draw ImGui. Uses C++ imgui_render_draw_data for complex nested loops.
   The pure jank approach doesn't work because:
   1. Native pointers (ImDrawVert*, ImDrawIdx*) can't be captured in closures
   2. Boxing/unboxing on every iteration is complex and error-prone
   3. Function pointer types (ImDrawCallback) can't be checked with == nullptr"
  []
  (cpp/imgui_render_draw_data))
```

## Patterns That DO Work in Pure jank

| Pattern | Works? | Why |
|---------|--------|-----|
| Simple C API calls | YES | No closures needed |
| Single-level loops with jank values | YES | Loop variables are jank objects |
| Field access (`cpp/.-field obj`) | YES | Direct member access |
| Type conversions (`cpp/float. val`) | YES | Compile-time conversion |
| Boxing for storage (`cpp/box ptr`) | YES | Wraps in jank object |
| Unboxing for use (`cpp/unbox Type box`) | YES | Extracts typed pointer |

## Patterns That DON'T Work

| Pattern | Works? | Why |
|---------|--------|-----|
| Native types in doseq/dotimes | NO | Creates closure |
| Native types in for comprehensions | NO | Creates closure |
| Function pointer null checks | NO | Need C++ helper |
| Pointer array indexing | NO | No `[]` operator |
| Complex nested loops with native types | NO | Closures at every level |

## Key Lessons

1. **Prefer simple wrappers over complex pure jank** - A one-line `(cpp/fn-call)` is better than 50 lines of boxing/unboxing
2. **Closures are the boundary** - Native types can be used freely until you enter a closure
3. **C++ helpers are acceptable** - For complex operations, a small C++ helper is cleaner than fighting the type system
4. **Document why** - Future attempts to "purify" this code should know what was tried

## Files Modified

- `src/my_integrated_demo.jank` - Updated `imgui-draw!` docstring with explanation; removed unused C++ helpers
- `ai/20251205-imgui-render-jank-limitations.md` - This documentation

## Cleanup Performed

Removed these unused C++ helper functions from `cpp/raw`:
- `imgui_get_cmd_list` - pointer array indexing
- `imgui_get_draw_vert` - vertex pointer access
- `imgui_get_draw_idx` - index access
- `imgui_get_draw_cmd` - command pointer access
- `imgui_cmd_has_no_callback` - callback null check
- `imgui_render_triangles` - triangle rendering loop

Kept `opaque_box_ptr` as it's used by Jolt wrappers.

## What's Next

Consider these for future jank improvements:
1. Add native type capture support in closures (with explicit boxing at capture boundary)
2. Add `cpp/[]` operator for pointer array indexing
3. Add function pointer comparison support
