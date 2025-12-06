# Pure jank Jolt Wrappers with cpp/opaque_box_ptr

**Date**: 2025-12-05

## What Was Learned

### Converting C++ Jolt wrappers to pure jank

Successfully converted multiple Jolt C++ wrapper functions to pure jank by using:
1. `cpp/opaque_box_ptr` to extract raw `void*` from opaque_box
2. Direct calls to `jolt/jolt_*` functions via header require
3. Type conversions with `cpp/float.`, `cpp/uint32_t.`, etc.

### Key Pattern: cpp/opaque_box_ptr

The `cpp/opaque_box_ptr` helper (defined in cpp/raw) extracts the raw pointer from a jank opaque_box:

```cpp
// cpp/raw definition
inline void* opaque_box_ptr(jank::runtime::object_ref box) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(box);
    return o->data.data;
}
```

This enables direct calls to C APIs:

```clojure
;; Before: C++ wrapper
(defn optimize! [w] (cpp/jolt_optimize w))

;; After: Pure jank with header require
(defn optimize! [w] (jolt/jolt_world_optimize_broad_phase (cpp/opaque_box_ptr w)))
```

### Type conversions for C API calls

When calling C APIs directly, explicit type conversions are needed:

| jank Type | C++ Type | Conversion |
|-----------|----------|------------|
| double | float | `(cpp/float. val)` |
| int64_t | uint32_t | `(cpp/uint32_t. val)` |
| int64_t | int | use directly |

## Changes Made

### 1. Converted Jolt wrappers to pure jank
**File**: `src/my_integrated_demo.jank`

```clojure
;; Before: C++ wrappers
(defn set-velocity! [w id vx vy vz] (cpp/jolt_set_vel w id vx vy vz))
(defn step! [w dt] (cpp/jolt_step w dt))
(defn optimize! [w] (cpp/jolt_optimize w))
(defn num-active [w] (cpp/jolt_num_active w))

;; After: Pure jank with jolt/ header require
(defn set-velocity! [w id vx vy vz]
  (jolt/jolt_body_set_velocity (cpp/opaque_box_ptr w) (cpp/uint32_t. id) (cpp/float. vx) (cpp/float. vy) (cpp/float. vz)))
(defn step! [w dt] (jolt/jolt_world_step (cpp/opaque_box_ptr w) (cpp/float. dt) 1))
(defn optimize! [w] (jolt/jolt_world_optimize_broad_phase (cpp/opaque_box_ptr w)))
(defn num-active [w] (jolt/jolt_world_get_num_active_bodies (cpp/opaque_box_ptr w)))
```

Also already converted earlier:
```clojure
(defn num-bodies [w] (jolt/jolt_world_get_num_bodies (cpp/opaque_box_ptr w)))
```

### 2. Removed C++ wrapper functions from cpp/raw

Removed these functions (no longer needed):
- `jolt_set_vel` - now `jolt/jolt_body_set_velocity`
- `jolt_step` - now `jolt/jolt_world_step`
- `jolt_optimize` - now `jolt/jolt_world_optimize_broad_phase`
- `jolt_num_active` - now `jolt/jolt_world_get_num_active_bodies`

### 3. Updated ImGui panel to use jank wrappers

```clojure
;; Before: Direct C++ calls
(imgui/Text #cpp "Active: %d" (cpp/jolt_num_active w))
(imgui/Text #cpp "Flecs Entities: %d" (cpp/flecs_entity_count))

;; After: Using jank wrappers
(imgui/Text #cpp "Active: %d" (num-active w))
(imgui/Text #cpp "Flecs Entities: %d" (entity-count))
```

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## Summary of Conversions

| Old Pattern (C++ wrapper) | New Pattern (Pure jank) |
|---------------------------|-------------------------|
| `(cpp/jolt_optimize w)` | `(jolt/jolt_world_optimize_broad_phase (cpp/opaque_box_ptr w))` |
| `(cpp/jolt_num_active w)` | `(jolt/jolt_world_get_num_active_bodies (cpp/opaque_box_ptr w))` |
| `(cpp/jolt_step w dt)` | `(jolt/jolt_world_step (cpp/opaque_box_ptr w) (cpp/float. dt) 1)` |
| `(cpp/jolt_set_vel w id vx vy vz)` | `(jolt/jolt_body_set_velocity (cpp/opaque_box_ptr w) (cpp/uint32_t. id) ...)` |

## What's Still in cpp/raw

These still need C++ wrappers due to complexity:
- `jolt_create_world` - Creates and boxes the world
- `jolt_destroy_world` - Unboxes and destroys (updates global state)
- `jolt_create_sphere` - Returns body ID, handles type conversions
- `jolt_create_floor` - Creates static floor body
- ImGui rendering functions - Complex loops/callbacks
- Entity position getters - Uses out parameters and global state

## Key Lessons

1. **cpp/opaque_box_ptr enables direct C API calls** - No need for C++ wrappers for simple void*/pointer extractions

2. **Type conversions are explicit in jank** - Use `cpp/float.`, `cpp/uint32_t.` etc. when calling C APIs expecting specific types

3. **Header requires work seamlessly** - `["jolt_c.h" :as jolt :scope ""]` gives direct access to all jolt_* functions

4. **Less cpp/raw = cleaner code** - Each conversion removes opaque C++ and makes the code more transparent

5. **#cpp format strings need raw C values, not jank wrappers** - When using ImGui::Text with `#cpp "format %d"`:
   - **BAD**: `(imgui/Text #cpp "Count: %d" (my-jank-wrapper))` - prints memory address!
   - **GOOD**: `(imgui/Text #cpp "Count: %d" (cpp/.size (cpp/get_entities)))` - prints actual value
   - Jank wrapper functions return boxed integers, but `#cpp` expects raw C values
   - Call C functions directly in #cpp expressions for correct display

## What's Next

- Consider converting more Jolt wrappers (`create_sphere`, `create_floor`) if feasible
- Look at Flecs wrappers for similar conversion opportunities
- Keep ImGui complex rendering in cpp/raw (too many callbacks/loops)
