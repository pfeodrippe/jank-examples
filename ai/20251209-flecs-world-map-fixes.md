# Flecs World Map Interface - Bug Fixes

## Date: 2025-12-09

## Summary

Fixed multiple issues preventing the VybeFlecsWorld/VybeFlecsEntity map interface from working correctly with component registration and tag handling.

## Issues Fixed

### 1. Missing `vybe_ptr_to_int64` C++ Helper

**Problem**: The component ID cache used `get-world-identity` which called `cpp/vybe_ptr_to_int64` to convert world pointers to stable integer keys for caching. This function didn't exist.

**Solution**: Added the C++ helper to `src/vybe/flecs.jank`:
```cpp
// Convert a pointer to int64 for use as a stable cache key
int64_t vybe_ptr_to_int64(ecs_world_t* ptr) {
  return reinterpret_cast<int64_t>(ptr);
}
```

### 2. Tag Entity Creation on Add

**Problem**: When adding keyword tags like `:walking` to entities, the `eid` function was doing `ecs_lookup` which returns 0 if the entity doesn't exist. Then `ecs_add_id` was called with component ID 0, causing a fatal error.

**Error**: `invalid component '#0' passed to add() for entity 'alice': components cannot be 0`

**Solution**: Modified `-add-item-to-entity!` to create tag entities if they don't exist:
```clojure
(keyword? item)
;; For tags, create the entity if it doesn't exist, then add to target
(let* [w (cpp/unbox (cpp/type "ecs_world_t*") w-box)
       existing-id (fl/ecs_lookup w (name item))
       tag-id (if (> existing-id 0)
                existing-id
                (cpp/vybe_create_entity_with_name w (name item)))
       _ (fl/ecs_add_id w e-id tag-id)]
  nil)
```

### 3. jank `disj` Not Supporting User Types

**Problem**: jank's core `disj` function doesn't recognize user types as disjoinable collections, even if they implement a `:disj` method.

**Error**: `not disjoinable: #{:alice walking, (Identifier,Name), (Identifier,Symbol)}`

**Solution**:
1. Added a new function `vf/remove-id!` that removes tags/components from entities:
```clojure
(defn remove-id!
  "Remove a component or tag from an entity."
  [entity tag-or-comp]
  (let [w-box (:vf/world-box entity)
        e-id (:vf/entity-id entity)
        w (cpp/unbox (cpp/type "ecs_world_t*") w-box)]
    (cond
      (keyword? tag-or-comp)
      (let [c-id (fl/ecs_lookup w (name tag-or-comp))]
        (when (> c-id 0)
          (cpp/vybe_remove_id w e-id c-id)))
      (int? tag-or-comp)
      (cpp/vybe_remove_id w e-id tag-or-comp)
      :else nil)
    entity))
```

2. Updated test to use `vf/remove-id!` instead of `disj`:
```clojure
(vf/remove-id! alice :walking)  ;; instead of (disj alice :walking)
```

## Files Modified

- `src/vybe/flecs.jank` - Added C++ helpers and `remove-id!` function, fixed tag creation
- `src/vybe/type.jank` - Removed debug output from `comp-id` and `register-comp!`
- `test/vybe/flecs_test.jank` - Updated test to use `vf/remove-id!`

## Test Results

All tests pass:
- 16 flecs tests (30 assertions)
- 19 type tests (83 assertions)
- Total: 35 tests, 113 assertions

## Commands

```bash
./run_tests.sh
```

## API Summary

### World Map Interface
```clojure
;; Create world with map interface
(def w (vf/make-world-map))

;; Register components
(vt/register-comp! w Position)
(vt/register-comp! w Velocity)

;; Add entities with assoc
(assoc w :bob [:walking (Position {:x 10.0 :y 20.0})])
(assoc w :alice [(Position {:x 5.0 :y 15.0})])

;; Check entity existence
(contains? w :bob)  ;; => true

;; Get entity
(get w :bob)  ;; => VybeFlecsEntity

;; Get component via get-in
(get-in w [:bob Position])  ;; => Position instance
(get-in w [:bob Position :x])  ;; => 10.0

;; Merge multiple entities
(merge w {:player1 [(Position {:x 0.0 :y 0.0})]
          :player2 [(Position {:x 100.0 :y 0.0})]})

;; Remove tag from entity
(vf/remove-id! (get w :bob) :walking)
```

## Notes

- The component ID cache uses pointer-to-int64 conversion for stable keys across world instances
- Tags are created as named entities if they don't exist when first added
- jank's core `disj` doesn't support user types, so use `vf/remove-id!` instead
