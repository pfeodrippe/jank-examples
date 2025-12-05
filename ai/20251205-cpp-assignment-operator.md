# cpp/= Assignment Operator in jank

**Date**: 2025-12-05

## What Was Learned

### cpp/= for C++ Assignment
jank supports `cpp/=` for assigning values to C++ lvalues (references or dereferenced pointers):

```clojure
;; Assign to a reference returned by a function
(cpp/= (cpp/get_view_offset_x) (cpp/float. 640.0))  ;; get_view_offset_x() = 640.0f
```

### No need for let* with _ for side effects
When calling void-returning or side-effect functions, you don't need `let*` with `_` bindings. Just call them sequentially:

```clojure
;; SIMPLE: Just call sequentially for pure side effects
(defn do-stuff! []
  (cpp/some_void_function arg1 arg2)
  (cpp/++ counter)
  nil)
```

`let*` with `_` is only needed when mixing value bindings with side-effect operations.

## Changes Made

### 1. Moved `imgui_reset_view` from C++ to pure jank
**File**: `src/my_integrated_demo.jank`

Before (C++ in cpp/raw):
```cpp
inline void imgui_reset_view() {
    get_view_offset_x() = 640.0f;
    get_view_offset_y() = 500.0f;
    get_view_scale() = 15.0f;
}
```

After (pure jank):
```clojure
(defn imgui-reset-view!
  "Reset view to default position and scale. Pure jank using cpp/= for assignment."
  []
  (cpp/= (cpp/get_view_offset_x) (cpp/float. 640.0))
  (cpp/= (cpp/get_view_offset_y) (cpp/float. 500.0))
  (cpp/= (cpp/get_view_scale) (cpp/float. 15.0))
  nil)
```

### 2. Updated call site
Changed from `(cpp/imgui_reset_view)` to `(imgui-reset-view!)`

### 3. Updated native resources guide
**File**: `ai/20251202-native-resources-guide.md`

Added new "Assignment Operator" section documenting `cpp/=` usage and updated "Void Return Handling" section to clarify that `let*` with `_` is often not necessary.

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## What's Next

- Continue converting other C++ helper functions to pure jank using cpp/ prefix
- Look for more opportunities to use `cpp/=` for assignments instead of C++ wrapper functions
- Consider converting `native_set_view_offset`, `native_add_view_offset`, `native_scale_view` to pure jank
