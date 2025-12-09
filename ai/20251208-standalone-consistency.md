# Session: Standalone Instance Consistency

**Date**: 2025-12-08
**Status**: COMPLETED - All 28 tests pass (9 flecs + 19 type)

## What Was Implemented

### Problem
Standalone component instances (created via `(Position {:x 1.0 :y 2.0})`) required `@` for keyword access (`(:x @pos)`), while ComponentRefs from `get-comp` did not (`(:x pos)`). This was inconsistent.

### Solution
Changed standalone instances from atoms to plain maps, making them consistent with ComponentRefs:

```clojure
;; Before (required @)
(let [pos (Position {:x 1.0 :y 2.0})]
  (:x @pos))  ;; needed @ because pos was an atom

;; After (no @ needed)
(let [pos (Position {:x 1.0 :y 2.0})]
  (:x pos))  ;; works directly, pos is a plain map
```

### Key Changes

1. **`create-standalone-instance`** - Now returns a plain map instead of an atom
2. **`standalone-instance?`** - Checks for `(and (map? x) (contains? x :vt/ptr-box))`
3. **`standalone-get`** - Works with plain maps (no deref)
4. **`standalone-merge!`** - Writes to native memory, returns instance unchanged
5. **`merge!`** (2-arity) - Updated to check `(contains? instance-or-ref :vt/ptr-box)`
6. **`vget`** - New function to read fresh values from native memory

### How Keyword Access Works Now

Both standalone instances and ComponentRefs are plain maps:

| Type | Keyword Access | Fresh Read |
|------|----------------|------------|
| Standalone | `(:x pos)` returns snapshot | `(vt/vget pos :x)` reads from memory |
| ComponentRef | `(:x pos)` returns snapshot | `(vt/vget pos :x)` reads from memory |

**Snapshot vs Fresh Read:**
- `(:x pos)` - Returns the value stored in the map (snapshot from creation/fetch time)
- `(vt/vget pos :x)` - Reads current value from native memory

### New `vget` Function

```clojure
(defn vget
  "Get a field value from native memory. Works with both standalone instances and ComponentRefs.
   Unlike keyword access (which returns a snapshot), this always reads fresh values from memory."
  [instance-or-ref field-kw]
  ...)
```

Usage:
```clojure
(let [pos (Position {:x 1.0 :y 2.0})]
  (vt/merge! pos {:x 99.0})
  ;; Snapshot still has old value
  (:x pos)          ;; => 1.0
  ;; vget reads from native memory
  (vt/vget pos :x)) ;; => 99.0
```

## Files Modified

- **`src/vybe/type.jank`**:
  - Moved type predicate functions (`is-f32-type?`, etc.) earlier in file
  - Moved `write-field-to-ptr!` before `merge!`
  - Updated `standalone-instance?` to check for map with `:vt/ptr-box`
  - Updated `standalone-get` and `standalone-merge!` for plain maps
  - Updated `merge!` to handle plain map standalone instances
  - Added `vget` function for fresh memory reads
  - Fixed `read-field-from-ptr` to use `void*` instead of `const void*`

- **`test/vybe/type_test.jank`**:
  - Updated `comp-test` to use `vt/vget` instead of `@`
  - Fixed typo in `get-field-direct-test` (42.7 -> 42.5)

## Commands Run

```bash
./run_tests.sh
# Testing vybe.flecs-test - 9 tests, 12 assertions
# Testing vybe.type-test - 19 tests, 66 assertions
# All tests passed!
```

## Jank Limitation Note

Jank's type system doesn't support custom object types with keyword lookup (no `reify`, `deftype`, or custom `associatively_readable` types from user code). Therefore:
- Keyword access `(:x pos)` cannot read from native memory - it reads from the map
- Use `vt/vget` for fresh values after `merge!`

The API is now consistent - no `@` needed for either standalone instances or ComponentRefs.
