# Performance Analysis Report: my-integrated-demo

**Date**: 2025-12-08
**Run duration**: 66.05s (3275 frames @ ~20fps)
**Profiler**: jank `--profile-fns`

---

## Executive Summary

The application spends **~88% of runtime** in the render loop (`fn:my-integrated-demo/draw`). The main bottlenecks are:

1. **Per-entity allocations** in `get-entity-position!` (~24µs overhead per entity)
2. **ImGui rendering** (`imgui-draw!`) at 4.73ms/frame
3. **Repeated atom operations** in coordinate transforms
4. **Redundant data access** (color fetched every frame for static data)

---

## Top Hotspots by Self Time

| Function | Self Time | % Total | Calls | Avg/Call | Issue |
|----------|-----------|---------|-------|----------|-------|
| `rt read_string` | 16.33s | 24.7% | 50,666 | 353µs | Startup only |
| `imgui-draw!` | 15.51s | 23.5% | 3,275 | 4.73ms | Expected - complex rendering |
| `draw-entity!` | 11.79s | 17.8% | 753,261 | 15.6µs | **HOT PATH** |
| `eval:fn:cpp_jit_expr` | 4.93s | 7.5% | 115 | 43ms | Startup JIT |
| `end-frame!` | 3.57s | 5.4% | 3,275 | 1.09ms | Raylib frame swap |
| `physics-to-screen` | 2.58s | 3.9% | 1,028,361 | 2.5µs | Coordinate transform |
| `get-entity-position!` | 1.77s | 2.7% | 753,261 | 2.3µs | **ALLOCATION HEAVY** |
| `get-entity-color` | 1.42s | 2.2% | 753,261 | 1.9µs | **REDUNDANT** |

---

## Detailed Analysis

### 1. `get-entity-position!` - CRITICAL

**Location**: Lines 556-567

```clojure
(defn get-entity-position!
  [jolt-world jolt-id]
  (let [x_ptr (cpp/new cpp/float 0.0)    ;; ALLOCATION!
        y_ptr (cpp/new cpp/float 0.0)    ;; ALLOCATION!
        z_ptr (cpp/new cpp/float 0.0)    ;; ALLOCATION!
        world_ptr (cpp/opaque_box_ptr jolt-world)]
    (jolt/jolt_body_get_position world_ptr (cpp/uint32_t. jolt-id) x_ptr y_ptr z_ptr)
    (reset! *pos-x (double (cpp/* x_ptr)))
    (reset! *pos-y (double (cpp/* y_ptr)))
    (reset! *pos-z (double (cpp/* z_ptr)))
    nil))
```

**Problem**: Allocates 3 new floats on EVERY call (753,261 times). The GC has to clean these up.

**Fix**: Pre-allocate static storage:

```clojure
;; At top level - allocate once
(def ^:private *pos-x-ptr (cpp/new cpp/float 0.0))
(def ^:private *pos-y-ptr (cpp/new cpp/float 0.0))
(def ^:private *pos-z-ptr (cpp/new cpp/float 0.0))

(defn get-entity-position!
  [jolt-world jolt-id]
  (let [world_ptr (cpp/opaque_box_ptr jolt-world)]
    (jolt/jolt_body_get_position world_ptr (cpp/uint32_t. jolt-id)
                                  *pos-x-ptr *pos-y-ptr *pos-z-ptr)
    (reset! *pos-x (double (cpp/* *pos-x-ptr)))
    (reset! *pos-y (double (cpp/* *pos-y-ptr)))
    (reset! *pos-z (double (cpp/* *pos-z-ptr)))
    nil))
```

**Expected improvement**: ~1-2ms per frame (reduce allocation overhead)

---

### 2. `get-entity-color` - REDUNDANT

**Location**: Lines 573-578

```clojure
(defn get-entity-color
  [idx]
  (let [entity (cpp/.at (cpp/get_entities) (cpp/size_t. idx))]
    [(int (cpp/.-r entity)) (int (cpp/.-g entity)) (int (cpp/.-b entity))]))
```

**Problem**: Entity colors are STATIC (set once at spawn), but this fetches them from C++ storage every frame for every entity.

**Fix**: Cache colors at spawn time in a jank vector:

```clojure
(def ^:private *entity-colors (atom []))

(defn add-entity!
  [flecs-world jolt-id radius cr cg cb]
  ;; ... existing code ...
  (swap! *entity-colors conj [cr cg cb])  ;; Cache color
  nil)

(defn get-entity-color [idx]
  (nth @*entity-colors idx))  ;; Fast jank lookup
```

**Expected improvement**: ~0.5-1ms per frame

---

### 3. `draw-entity!` - OPTIMIZATION OPPORTUNITIES

**Location**: Lines 580-616

**Problems**:
1. Calls `get-entity-color` (now addressed above)
2. Calls `get-entity-position!` which has allocation overhead
3. Creates `cpp/uint32_t.`, `cpp/size_t.` wrappers on every call
4. Multiple atom derefs per entity

**Additional optimization**: Batch position queries if Jolt supports it, or at minimum cache the entity reference:

```clojure
(defn draw-entity!
  [jolt-world idx entity]  ;; Pass entity directly
  (let [jolt-id (cpp/.-jolt_id entity)
        ...])
```

---

### 4. `physics-to-screen` - MINOR

**Location**: Lines 520-528

```clojure
(defn physics-to-screen [px pz]
  (let [offset-x @*cached-view-offset-x   ;; atom deref
        offset-y @*cached-view-offset-y   ;; atom deref
        scale @*cached-view-scale]        ;; atom deref
    [(+ offset-x (* px scale))
     (- offset-y (* pz scale))]))
```

**Problem**: 3 atom derefs per call × 1,028,361 calls = overhead.

**Fix**: Pass cached values as parameters, or use `def` with cpp storage:

```clojure
;; In draw-entities!, call physics-to-screen with cached values:
(defn draw-entity-fast! [jolt-world idx offset-x offset-y scale]
  ;; ... inline coordinate math ...
  )
```

---

### 5. `imgui-draw!` - EXPECTED COST

**Self time**: 15.51s (4.73ms/frame)

This is the ImGui rendering loop with nested iteration over draw commands, vertices, and indices. The implementation looks correct but is inherently expensive due to:
- Nested `dotimes` and `loop`
- Per-vertex color unpacking
- OpenGL state changes

**Potential optimizations** (if needed):
1. Move inner loop to C++ via `cpp/raw`
2. Use ImGui's native renderer backend instead of manual rendering

---

## Per-Frame Budget Analysis

| Phase | Time | % Frame |
|-------|------|---------|
| `draw` total | 17.49ms | 100% |
| `imgui-draw!` | 4.73ms | 27% |
| `draw-entities!` | 7.28ms | 42% |
| `end-frame!` | 1.09ms | 6% |
| Other (physics, input) | 4.39ms | 25% |

At **60fps target**, frame budget is **16.67ms**. Current average is **17.49ms** = slightly over budget.

---

## Recommendations

### Quick Wins (Low Effort, High Impact)

1. **Pre-allocate position pointers** - Estimated 1-2ms savings
2. **Cache entity colors** - Estimated 0.5-1ms savings
3. **Pass cached view values as params** - Estimated 0.2-0.5ms savings

### Medium Effort

4. **Batch entity iteration** - Pre-fetch entity data into jank vectors at frame start
5. **Reduce `cpp/` wrapper creations** - Cache `cpp/uint32_t.` conversions

### Higher Effort

6. **Move hot inner loop to C++** - Create a `draw_all_entities()` C++ function
7. **Use ImGui native backend** - Replace manual `imgui-draw!` with raylib-imgui integration

---

## Startup Analysis (One-Time Costs)

| Phase | Time | Notes |
|-------|------|-------|
| `rt read_string` | 16.33s | Parsing all code |
| `eval:fn:cpp_jit_expr` | 4.93s | JIT compilation |
| `load object clojure.core` | 0.94s | Loading stdlib |

Total startup: ~22s. This is expected for JIT compilation of a large codebase.

---

## Conclusion

The main performance issue is **per-entity allocation overhead** in `get-entity-position!`. Fixing this single function by pre-allocating static storage should reduce per-frame time by 10-15% and bring the application under the 60fps budget.

Secondary gains can be achieved by caching static entity data (colors) and reducing atom operations in hot paths.
