# iOS JIT Type Mismatch Error Fix Plan

## Date: 2026-01-02

## Error Summary

After running for ~90 frames, the iOS JIT app crashes with:
```
[EXPECT_OBJECT] type mismatch: got 160 (unknown) expected 2 (integer) ptr=0x13d214cc0
[EXPECT_OBJECT] Memory dump (first 64 bytes at ptr): a0 4c 21 3d 1 0 0 0 ...
```

## Analysis

### What Type 160 Means
- Type 160 (0xa0) is NOT a valid jank object type
- The memory dump shows bytes that look like pointers: `a0 4c 21 3d 1 0 0 0` = 0x13d214ca0
- This is a native pointer/struct being interpreted as a jank object
- The first byte (0xa0 = 160) is being read as the object type tag

### Root Cause
A C++ function is returning a raw native value/pointer that jank is trying to interpret as a boxed integer object. This works differently in AOT vs JIT:
- **AOT**: The generated code uses `convert<int>::into_object()` which properly boxes the value
- **JIT**: Something in the codegen or runtime is returning unboxed native values

### When It Happens
- After ~90 successful frames (camera sync works, angle-y increments properly)
- Happens during the draw loop, after `sync-camera-from-cpp!` but before frame completion
- Not an initialization issue - runs fine for many frames first

### Likely Culprits
Looking at `ui/draw-debug-ui!` which is called every frame:

1. **Line 88**: `(> (sdfx/get_mesh_preview_triangle_count) 0)` - comparison with 0
2. **Line 108**: `(cpp/int. (:selected-object edit))` - I already fixed the source
3. **Line 126-138**: Resolution comparisons: `(< res 64)`, `(+ res step)`, etc.
4. **Line 140-142**: `(sdfx/get_mesh_preview_vertex_count)`, `get_mesh_preview_triangle_count`

## Fix Strategy

### Immediate Fix: Add Defensive Integer Handling

Add a helper function to safely convert potentially-corrupted values to integers:

```clojure
(defn safe-int
  "Safely convert a value to integer, returning default if nil or invalid."
  ([v] (safe-int v 0))
  ([v default]
   (if (number? v)
     v
     default)))
```

Then update `draw-debug-ui!` to use it for all sdfx function returns.

### Specific Fixes

**File: `/Users/pfeodrippe/dev/something/src/vybe/sdf/ui.jank`**

1. Line 85-88: Mesh visibility check
```clojure
;; BEFORE:
(let [mesh-visible (sdfx/get_mesh_preview_visible)
      ...
      mesh-has-indices (> (sdfx/get_mesh_preview_triangle_count) 0)]

;; AFTER:
(let [mesh-visible (sdfx/get_mesh_preview_visible)
      ...
      tri-count (or (sdfx/get_mesh_preview_triangle_count) 0)
      mesh-has-indices (> tri-count 0)]
```

2. Lines 126-138: Resolution controls
```clojure
;; BEFORE:
(let [res (sdfx/get_mesh_preview_resolution)
      step (cond
             (< res 64) 8
             ...)]

;; AFTER:
(let [res (or (sdfx/get_mesh_preview_resolution) 64)
      step (cond
             (< res 64) 8
             ...)]
```

3. Lines 140-142: Mesh stats
```clojure
;; BEFORE:
(let [verts (sdfx/get_mesh_preview_vertex_count)
      tris (sdfx/get_mesh_preview_triangle_count)]
  (imgui/Text #cpp "  Verts: %d  Tris: %d" (cpp/int. verts) (cpp/int. tris)))

;; AFTER:
(let [verts (or (sdfx/get_mesh_preview_vertex_count) 0)
      tris (or (sdfx/get_mesh_preview_triangle_count) 0)]
  (imgui/Text #cpp "  Verts: %d  Tris: %d" (cpp/int. verts) (cpp/int. tris)))
```

4. Line 174: Export resolution
```clojure
;; BEFORE:
(let [res (sdfx/get_mesh_preview_resolution)
      ...]

;; AFTER:
(let [res (or (sdfx/get_mesh_preview_resolution) 64)
      ...]
```

## Implementation Steps

1. Edit `/Users/pfeodrippe/dev/something/src/vybe/sdf/ui.jank`:
   - Add `(or ... default)` wrappers around all `sdfx/get_mesh_*` calls
   - Ensure all values passed to `(cpp/int. ...)` are valid numbers

2. Rebuild and test:
   ```bash
   make ios-jit-clean && make ios-jit-sim-run 2>&1 | tee /tmp/ios-jit-test.txt
   ```

3. Verify app runs without crashing for extended periods

## Success Criteria

- [ ] App runs continuously without "type mismatch" errors
- [ ] Mesh preview controls work correctly
- [ ] Export functionality works
- [ ] Debug UI displays properly

## Notes

The underlying issue is likely in how JIT-generated code handles C++ function returns vs AOT. This is a workaround that adds defensive nil-handling. The proper fix would be in jank's codegen to ensure all C++ returns are properly boxed.
