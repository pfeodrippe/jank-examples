# CPU Analysis: draw-debug-ui! Performance Issue

**Date**: 2025-12-16
**Issue**: CPU usage jumps from 48% to 106% when `draw-debug-ui!` is called
**File**: `src/vybe/sdf/ui.jank`

## Summary

The CPU spike is likely caused by a combination of:
1. **Redundant sdfx sync calls every frame** (high impact)
2. **Potential continuous mesh regeneration bug** (high impact)
3. **JIT call overhead** for many small FFI calls (medium impact)
4. **GC pressure from transient allocations** (lower impact than expected)

## Investigation Findings

### What Does NOT Allocate Per Frame

Contrary to initial suspicion, these are **NOT the problem**:

1. **`cpp/float.`, `cpp/int.`, `cpp/bool.`** - Stack allocated
   - Codegen produces: `float __jank_cpp_ctor_0{ static_cast<float>(10.0) };`
   - No heap allocation

2. **`cpp/unbox`** - Just pointer extraction
   - Returns the raw pointer stored in opaque_box
   - No new allocation

3. **Atom derefs** (`@*camera*`, `@*edit-mode*`, `@*objects*`)
   - Clojure persistent data structures share structure
   - Deref returns same reference, not a copy

4. **`defonce` pointers** (`*export-colors`, etc.)
   - Created once at startup via `v->p` macro
   - Only `cpp/box` (one-time allocation)

### Real Problems Identified

#### 1. Redundant sdfx Sync Calls (MAJOR)

Every frame unconditionally calls:
```clojure
(sdfx/set_mesh_fill_with_cubes (cpp/bool. ...))
(sdfx/set_mesh_voxel_size ...)
(sdfx/set_mesh_use_gpu_dc (cpp/bool. ...))
(sdfx/set_mesh_use_vertex_colors (cpp/bool. ...))
```

**Problem**: These C++ FFI calls happen even when values haven't changed. Each call:
- Goes through jank's JIT
- Potentially triggers state changes in sdfx engine
- May invalidate caches or trigger dirty flags

#### 2. Potential Mesh Regeneration Loop (CRITICAL)

Lines 170-174:
```clojure
(when (and (sdfx/get_mesh_preview_visible)
           (sdfx/mesh_preview_needs_regenerate))
  (println "REGENERATING MESH!") ;; Debug: should NOT print continuously!
  (sdfx/generate_mesh_preview (cpp/int. -1))
  (sdfx/clear_mesh_regenerate_flag))
```

The comment "should NOT print continuously!" suggests a bug where:
- `mesh_preview_needs_regenerate` keeps returning true
- This triggers expensive mesh regeneration every frame
- **Check if this println is appearing continuously!**

#### 3. Many FFI Calls Per Frame

Active code calls these sdfx functions every frame:
- `sdfx/get_mesh_preview_visible`
- `sdfx/get_mesh_preview_resolution`
- `sdfx/mesh_preview_needs_regenerate`
- Multiple `sdfx/set_*` calls

Each FFI call has overhead from:
- JIT function dispatch
- C++ function pointer invocation
- Potential stack frame setup

#### 4. What cpp/box DOES Allocate

When `cpp/box` is called, it creates:
```cpp
jank::runtime::make_box<jank::runtime::obj::opaque_box>(ptr, "type_string")
```

This allocates a GC object containing:
- `void* data` - the pointer
- `jtl::immutable_string canonical_type` - the type name
- `jtl::option<object_ref> meta` - optional metadata

However, in `draw-debug-ui!`, `cpp/box` is NOT called directly per frame.

## Recommended Fixes

### Immediate: Check for Regeneration Loop

```bash
# Run the app and watch for continuous output:
# If you see "REGENERATING MESH!" printed continuously, that's the bug
```

### Fix 1: Only Sync When Values Change

```clojure
;; Instead of:
(sdfx/set_mesh_fill_with_cubes (cpp/bool. (u/p->v *fill-with-cubes)))

;; Track previous values and only sync on change:
(let [new-val (u/p->v *fill-with-cubes)]
  (when (not= new-val @*last-fill-with-cubes)
    (reset! *last-fill-with-cubes new-val)
    (sdfx/set_mesh_fill_with_cubes (cpp/bool. new-val))))
```

### Fix 2: Batch FFI Calls

Instead of multiple small calls, create a single C++ function that sets all parameters:
```clojure
;; C++ side: void set_mesh_params(bool fill_cubes, float voxel_size, bool gpu_dc, ...)
(sdfx/set_mesh_params
  (cpp/bool. fill-cubes)
  (cpp/float. voxel-size)
  (cpp/bool. gpu-dc))
```

### Fix 3: Skip UI When Not Visible

```clojure
(defn draw-debug-ui! []
  (when (sdfx/engine_initialized)  ;; Already have this check in new-frame!
    ;; ... UI code
    ))
```

### Fix 4: Profile to Confirm

Use jank's `--profile-interop` flag to measure actual cpp/ call overhead.

## Commands Used

```bash
# Search for draw-debug-ui! definition
grep -rn "draw-debug-ui!" src/

# Read ui.jank
cat src/vybe/sdf/ui.jank

# Search jank for opaque_box implementation
grep -rn "opaque_box" ~/dev/jank/compiler+runtime/

# Check codegen for cpp/box
grep -rn "cpp_box" ~/dev/jank/compiler+runtime/src/cpp/jank/codegen/
```

## Next Steps

1. **Verify regeneration loop**: Check if "REGENERATING MESH!" prints continuously
2. **Profile sdfx calls**: Add timing around major sdfx functions
3. **Implement dirty flags**: Only sync state when UI values actually change
4. **Consider caching**: Cache expensive computations like mesh preview visibility
5. **Test with UI disabled**: Comment out `draw-debug-ui!` call to confirm it's the cause

## Code Structure (for reference)

```
draw-debug-ui!
├── Get state (cam, edit, objs, io, fps) - CHEAP
├── SetNextWindowPos + Begin - CHEAP
├── Text FPS - CHEAP
├── do block
│   ├── #_(do ...) - COMMENTED OUT (lines 53-117)
│   ├── when *use-dual-contouring - ACTIVE
│   │   ├── Checkbox "Fill With Cubes"
│   │   ├── sdfx/set_mesh_fill_with_cubes - EVERY FRAME
│   │   ├── when fill-with-cubes
│   │   │   ├── SliderFloat
│   │   │   └── sdfx/set_mesh_voxel_size - EVERY FRAME
│   │   ├── Checkbox "GPU DC"
│   │   └── sdfx/set_mesh_use_gpu_dc - EVERY FRAME
│   ├── Button "Regenerate Mesh"
│   ├── Separator + Text
│   ├── Checkbox "Include Colors"
│   ├── sdfx/set_mesh_use_vertex_colors - EVERY FRAME
│   ├── Checkbox "Include UVs"
│   ├── let [res ...] for Export buttons
│   ├── Button "View exported_scene.glb"
│   ├── when (auto-regenerate check) - POTENTIAL LOOP
│   └── imgui/End
```

## Learnings

1. **cpp/float., cpp/int., cpp/bool.** are cheap (stack allocated)
2. **cpp/box** allocates GC objects, but not called per-frame in this code
3. **cpp/unbox** just extracts pointer, no allocation
4. **Redundant FFI calls** are likely the main overhead
5. **Always check for infinite loops** when debugging performance

---

## New Profiling Macros Added

Added to `vybe.util`:

### Usage

```clojure
(require '[vybe.util :as u])

;; Enable metrics collection
(u/enable-metrics!)

;; Time a single form
(u/timed :my-operation
  (expensive-computation))

;; Time multiple forms with auto-generated keys
(u/with-metrics :draw-ui
  (imgui/Begin "Window")      ; timed as :draw-ui/imgui-Begin
  (imgui/Text "Hello")        ; timed as :draw-ui/imgui-Text
  (my-fn 1 2 3)               ; timed as :draw-ui/my-fn
  (imgui/End))                ; timed as :draw-ui/imgui-End

;; After running for a while, print results
(u/print-metrics)

;; Reset and start fresh
(u/reset-metrics!)

;; Disable when done
(u/disable-metrics!)
```

### Example Output

```
=== METRICS ===
Key                                          Count     Total(ms)     Avg(us)       Min       Max
------------------------------------------------------------------------------------------------
:BB/sdfx-set_mesh_render_solid                6922   2770.848000         400       360      3729
:BB/imgui-Text                               83061     95.878000           1         0        22
:BB/sdfx-set_mesh_use_gpu_dc                  6921   2775.917000         401       362      4106
:BB/let                                       2543  20462.572000        8046      7455     13090
:BB/imgui-Checkbox                           48449  19474.513000         401       362      3943
=======
```

**Key insight from metrics**: The `sdfx-set_*` FFI calls take ~400us each, and there are many of them per frame. The `imgui-Checkbox` also has high total time due to frequent calls.

### Profiling draw-debug-ui!

To profile, wrap the function body:

```clojure
(defn draw-debug-ui! []
  (u/profile :draw-debug-ui
    (let [cam (state/get-camera)
          ...]
      ;; ... rest of function
      )))
```

Or wrap individual suspicious sections:

```clojure
(u/timed :sync-mesh-params
  (do
    (sdfx/set_mesh_fill_with_cubes ...)
    (sdfx/set_mesh_voxel_size ...)
    (sdfx/set_mesh_use_gpu_dc ...)))

(u/timed :regenerate-check
  (when (and (sdfx/get_mesh_preview_visible)
             (sdfx/mesh_preview_needs_regenerate))
    (sdfx/generate_mesh_preview (cpp/int. -1))
    (sdfx/clear_mesh_regenerate_flag)))
```

### API Summary

| Function | Description |
|----------|-------------|
| `(enable-metrics!)` | Start collecting metrics |
| `(disable-metrics!)` | Stop collecting (forms still execute, just not timed) |
| `(reset-metrics!)` | Clear all collected data |
| `(print-metrics)` | Print sorted table of results |
| `(timed key form)` | Time a single form |
| `(with-metrics name & forms)` | Time each form in body |
| `(profile name & forms)` | Alias for with-metrics |

---

## Implementation Notes (2025-12-16 Update)

### now-us Implementation

The `now-us` function needed special handling because:

1. **`cpp/raw` with semicolon** returns `nil` (C++ statement, not expression)
2. **`cpp/raw` without semicolon** gets wrapped in `#ifndef` guard that breaks expression return
3. **`cpp/clojure.core_native.current_time`** isn't accessible from outside clojure.core namespace

**Solution**: Reference the private `current-time` function via var:

```clojure
(defn now-us
  "Get current time in microseconds (from nanoseconds)."
  []
  (quot (@#'clojure.core/current-time) 1000))
```

This uses:
- `#'clojure.core/current-time` - Gets the var for the private function
- `@` - Dereferences the var to call the function
- Returns nanoseconds, divided by 1000 for microseconds

### jank Missing Functions

The following Clojure functions are NOT yet implemented in jank:

- `format` - Returns "TODO: port format"
- `sort-by` - Not implemented
- `sort` - Not implemented
- `clojure.string/replace` - Returns "TODO: port clojure.string/replace"

Workarounds:
- Use `str` for string building instead of `format`
- Skip sorting for now (print unsorted)
- Use `map` with character comparison instead of `clojure.string/replace`
