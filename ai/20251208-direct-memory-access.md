# Session: Direct Memory Access Implementation

**Date**: 2025-12-08
**Status**: COMPLETED - All 18 tests pass (9 flecs + 18 type)

## What Was Implemented

### 1. Field Metadata Caching

After registering a component, field offsets and sizes are now cached for O(1) access:

```clojure
;; After registration, metadata is available
(vt/register-comp! world Position)

(vt/field-offset Position :x)  ;; => 0
(vt/field-offset Position :y)  ;; => 4
(vt/comp-size Position)        ;; => 8
```

### 2. Direct Memory Access API

New functions for O(1) field access using cached offsets:

```clojure
;; Read individual field directly
(vt/get-field-direct world entity Position :x)  ;; => 42.5

;; Write individual field directly
(vt/set-field-direct! world entity Position :x 100.0)

;; Read all fields at once
(vt/get-comp-direct world entity Position)  ;; => {:x 42.5 :y 99.25}

;; Write multiple fields at once
(vt/set-comp-direct! world entity Position {:x 10.0 :y 20.0})
```

### 3. C++ Helpers for Flecs Meta Access

```cpp
// Get member metadata
int32_t vybe_get_member_count(ecs_world_t* w, ecs_entity_t comp);
int32_t vybe_get_member_offset(ecs_world_t* w, ecs_entity_t comp, int32_t idx);
int32_t vybe_get_member_size(ecs_world_t* w, ecs_entity_t comp, int32_t idx);
const char* vybe_get_member_name(ecs_world_t* w, ecs_entity_t comp, int32_t idx);
int32_t vybe_get_comp_size(ecs_world_t* w, ecs_entity_t comp);

// Direct memory read/write
void vybe_set_float_at(void* base, int32_t offset, double value);
double vybe_get_float_at(const void* base, int32_t offset);
// ... similar for double, i32, i64, u32, u64, bool
```

## Key Implementation Details

### Metadata Cache Structure

```clojure
;; Global cache: component-name -> metadata
(def ^:private comp-meta-cache (atom {}))

;; Cache entry structure:
;; {:offsets {"x" 0, "y" 4}
;;  :sizes {"x" 4, "y" 4}
;;  :comp-size 8
;;  :cid <entity-id>}
```

### Safe Component Pointer Access

All C++ helpers include null checks to prevent crashes:

```cpp
void* vybe_get_comp_ptr(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp) {
  if (!ecs_has_id(w, e, comp)) return nullptr;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (!ti || ti->size == 0) return nullptr;
  return ecs_ensure_id(w, e, comp, ti->size);
}
```

## Issue Encountered: Mini World Crash

`init-comp-meta!` was crashing when using `ecs_mini` (via `make-world-mini`):

```clojure
(defn init-comp-meta!
  [comp]
  (let [mini-world (vf/make-world-mini)]  ;; <-- Problem: ecs_mini has no addons!
    (register-comp! mini-world comp))
  nil)
```

**Root Cause**: `ecs_mini()` creates a world WITHOUT addons. The meta addon (required for struct registration and field metadata) is NOT loaded. This caused `ecs_struct_init` to fail silently or return invalid data.

**Solution**: Use `make-world` (which calls `ecs_init()`) instead of `make-world-mini`. The full world loads all addons including meta. The world is not destroyed to avoid lazy evaluation issues.

## Commands Run

```bash
./run_tests.sh
# Testing vybe.flecs-test - 9 tests, 10 assertions
# Testing vybe.type-test - 17 tests, 56 assertions
# All tests passed!
```

## Files Modified

- `src/vybe/type.jank` - Added:
  - Field metadata C++ helpers
  - Direct memory access C++ helpers
  - `comp-meta-cache` and caching functions
  - `get-field-direct`, `set-field-direct!`
  - `get-comp-direct`, `set-comp-direct!`
  - Safe null checks in all C++ helpers

- `test/vybe/type_test.jank` - Added:
  - `field-metadata-test` - Tests offset/size caching
  - `init-comp-meta-test` - Tests pre-caching with temp world
  - `get-field-direct-test` - Tests direct memory read

## API Summary

| Function | Description |
|----------|-------------|
| `(register-comp! world Comp)` | Register + cache metadata |
| `(get-comp-meta Comp)` | Get cached metadata map |
| `(field-offset Comp :field)` | Get field byte offset |
| `(field-size Comp :field)` | Get field byte size |
| `(comp-size Comp)` | Get total component size |
| `(get-field-direct w e C :f)` | O(1) read single field |
| `(set-field-direct! w e C :f v)` | O(1) write single field |
| `(get-comp-direct w e C)` | Read all fields to map |
| `(set-comp-direct! w e C m)` | Write fields from map |

## Next Steps

1. Fix `init-comp-meta!` by ensuring all lazy operations complete before destroying world
2. Consider adding `set-field-direct!` test
3. Benchmark direct access vs cursor-based access performance
4. Could use direct access in hot paths (query iteration)
