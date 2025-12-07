# Fix: render-draw-data vertex indexing bug

**Date**: 2025-12-07

## Problem

ImGui panels were not rendering when using the jank `render-draw-data` function, but worked with `cpp/imgui_render_draw_data`.

## Root Cause

In the vertex rendering loop, the index was computed from the index buffer but never used:

```clojure
;; BEFORE (broken):
(let [idx (->* ib (aget (cpp/int (+ (->* pc * .-IdxOffset) j k))))
      v (->* vb &)     ;; BUG: This is &vb, not &vb[idx]!
      col (->* v * .-col)]
```

The C++ version correctly uses the index:
```cpp
const ImDrawVert* v = &vb[ib[pc->IdxOffset + j + k]];
```

## Fix

Use the computed index to access the correct vertex:

```clojure
;; AFTER (fixed):
(let [idx (->* ib (aget (cpp/int (+ (->* pc * .-IdxOffset) j k))))
      v (->* vb (aget idx) &)  ;; &vb[idx] - use index to get correct vertex
      col (->* v * .-col)]
```

## Lesson

When translating C++ array indexing like `&arr[idx]` to jank:
- `(->* arr &)` = `&arr` (address of pointer itself)
- `(->* arr (aget idx) &)` = `&arr[idx]` (address of element at index)

Always verify that computed indices are actually used in subsequent array accesses.

## File Changed

- `src/my_integrated_demo.jank:526` - Fixed vertex buffer indexing
