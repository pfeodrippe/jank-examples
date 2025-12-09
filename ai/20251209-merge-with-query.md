# Fix: Enable `merge` instead of `vt/merge!` in `with-query`

**Date**: 2025-12-09

## Summary

Modified `get-comp` to return user-type instances (like defcomp instances) instead of ComponentRef maps. This enables using standard `merge` in `with-query` instead of `vt/merge!`.

## Changes Made

### `src/vybe/type.jank`

1. **Added forward declaration** (line 7):
   ```clojure
   (declare create-comp-user-type)
   ```

2. **Modified `get-comp`** (lines 571-597):
   - Changed from returning a ComponentRef map to returning a user-type instance
   - User-type instances support `merge` natively through the `:conj` protocol
   - Now calls `init-comp-meta!` and `create-comp-user-type` to create instances backed by entity memory

   Before:
   ```clojure
   ;; Returned a map with :vt/world-box, :vt/entity, :vt/comp keys
   (assoc field-values :vt/world-box w-box :vt/entity entity :vt/comp desc)
   ```

   After:
   ```clojure
   ;; Returns user-type instance with :vt/ptr-box and :vt/comp
   (ut-factory {:ptr-box ptr-box :comp desc})
   ```

3. **Updated docstring for `with-query`** in `src/vybe/flecs.jank`:
   - Changed example to show `merge` usage instead of `vt/merge!`

### `test/vybe/flecs_test.jank`

- Changed `with-query-test` to use `merge` instead of `vt/merge!`:
  ```clojure
  (merge p {:x (+ (:x p) (:dx v))
            :y (+ (:y p) (:dy v))})
  ```

### `test/vybe/type_test.jank`

- Renamed `get-comp-returns-map-test` to `get-comp-returns-instance-test`
- Renamed `comp-ref-test` to `get-comp-instance-test`
- Updated tests to reflect that `get-comp` returns user-type instances with `:vt/ptr-box` and `:vt/comp` (instead of `:vt/world-box` and `:vt/entity`)
- Added test for `merge` working on entity-backed instances

## How It Works

User-type instances (created via `defcomp`) implement the map interface including `:conj`, which is used by `merge`. When you call `merge` on a user-type instance:

1. `merge` internally calls `conj` on the target with each entry from the source map
2. The user-type's `:conj` handler writes each field value directly to native memory
3. The same instance is returned (mutates in-place)

This allows natural Clojure-style code:
```clojure
(vf/with-query w [p Position, v Velocity]
  (merge p {:x (+ (:x p) (:dx v))
            :y (+ (:y p) (:dy v))}))
```

## Test Results

All 28 tests pass (9 flecs tests + 19 type tests).
