# Pure jank View Manipulation Functions

**Date**: 2025-12-05

## What Was Learned

### Moving C++ view mutators to pure jank using cpp/=

Successfully converted all three view manipulation functions from C++ (cpp/raw) to pure jank:

1. **scale-view!** - Scale view with clamping
2. **add-view-offset!** - Add to view offset (for panning)
3. **set-view-offset!** - Set view offset directly

### Pattern for read-modify-write with cpp/=

When you need to do `x += value` in C++, use a read-modify-write pattern in jank:

```clojure
(defn add-view-offset!
  "Add to view offset. Pure jank using cpp/= with read-modify-write."
  [dx dy]
  (let* [new-x (+ (cpp/get_view_offset_x) dx)
         new-y (+ (cpp/get_view_offset_y) dy)]
    (cpp/= (cpp/get_view_offset_x) (cpp/float. new-x))
    (cpp/= (cpp/get_view_offset_y) (cpp/float. new-y))
    nil))
```

### Function definition order matters in jank

Functions must be defined BEFORE they are called. If you add a helper function, place it before any function that uses it.

## Changes Made

### 1. Added pure jank view manipulation functions
**File**: `src/my_integrated_demo.jank` (lines 456-478)

```clojure
(defn scale-view!
  "Scale view by factor, clamped between 5.0 and 50.0. Pure jank."
  [factor]
  (let* [s (* (cpp/get_view_scale) factor)
         clamped (min 50.0 (max 5.0 s))]
    (cpp/= (cpp/get_view_scale) (cpp/float. clamped))
    nil))

(defn add-view-offset!
  "Add to view offset. Pure jank using cpp/= with read-modify-write."
  [dx dy]
  (let* [new-x (+ (cpp/get_view_offset_x) dx)
         new-y (+ (cpp/get_view_offset_y) dy)]
    (cpp/= (cpp/get_view_offset_x) (cpp/float. new-x))
    (cpp/= (cpp/get_view_offset_y) (cpp/float. new-y))
    nil))

(defn set-view-offset!
  "Set view offset directly. Pure jank using cpp/=."
  [x y]
  (cpp/= (cpp/get_view_offset_x) (cpp/float. x))
  (cpp/= (cpp/get_view_offset_y) (cpp/float. y))
  nil)
```

### 2. Updated call site in handle-input!

Changed from C++ wrapper to pure jank:
```clojure
;; Before
(cpp/native_add_view_offset dx dy)

;; After
(add-view-offset! dx dy)
```

### 3. Removed C++ functions from cpp/raw block

Removed these C++ functions (lines 211-226):
- `native_set_view_offset`
- `native_add_view_offset`
- `native_scale_view`

Added comment noting they're now in pure jank.

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## Summary of Pure jank View Functions

| Function | Purpose | C++ Equivalent |
|----------|---------|----------------|
| `imgui-reset-view!` | Reset view to defaults | `imgui_reset_view()` |
| `scale-view!` | Zoom in/out with clamping | `native_scale_view()` |
| `add-view-offset!` | Pan view (add to offset) | `native_add_view_offset()` |
| `set-view-offset!` | Set view position directly | `native_set_view_offset()` |

## What's Next

- Continue looking for more C++ functions in cpp/raw that can be converted to pure jank
- Look at ImGui helper functions that might be convertible
- Consider converting draw functions if they don't require complex C++ loops
