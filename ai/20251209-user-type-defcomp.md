# User-Type Based defcomp Implementation

## Date: 2025-12-09

## Summary

Implemented user-type based component instances for `defcomp`, enabling idiomatic Clojure map access syntax instead of requiring `vt/vget`.

## Problem

Previously, component instances created by `defcomp` were callable functions:
```clojure
(defcomp Position [[:x :float] [:y :float]])
(def pos (Position {:x 1.0 :y 2.0}))

;; Old way - required vget
(vt/vget pos :x)  ;; => 1.0

;; Desired way - idiomatic Clojure
(:x pos)          ;; Now works!
```

## Solution

Used jank's new `cpp/jank.runtime.make_user_type` to create custom types that implement the map interface.

### Key Changes to `src/vybe/type.jank`:

1. **Added `make-user-type` wrapper**:
   ```clojure
   (defn make-user-type [type-name constructor-fn]
     (cpp/jank.runtime.make_user_type type-name constructor-fn))
   ```

2. **Added `get-or-create-comp-user-type`** - creates a user-type factory for each component that implements:
   - `:get` - reads field from native memory (keyword access)
   - `:contains` - checks if field exists
   - `:count` - returns number of fields
   - `:to-string` - custom string representation

3. **Added `create-user-type-instance`** - allocates native memory and creates user-type instance

4. **Updated `make-comp-constructor`** to use `create-user-type-instance` instead of the old function-based approach

## Usage

```clojure
(defcomp Position [[:x :float] [:y :float]])

(def pos (Position {:x 1.0 :y 2.0}))

;; All these now work!
(:x pos)           ;; => 1.0 (keyword as function)
(get pos :y)       ;; => 2.0
(contains? pos :x) ;; => true
(count pos)        ;; => 2

;; Reads are always live from native memory
;; No need for vget for standalone instances
```

## Technical Details

- Each call to `(:x pos)` or `(get pos :x)` reads directly from native memory using offset-based access
- User-type factory is cached per component (one type per component name)
- Internal data stored: `{:ptr-box <boxed-ptr> :comp <descriptor>}`
- The map interface is "virtual" - no actual Clojure map is created, reads go directly to native memory
- Implementation reuses existing helpers: `read-field-from-ptr` and `write-field-to-ptr!` to avoid code duplication

## Tests

All 19 type tests pass, including new assertions for:
- `get` function
- `contains?` function
- `count` function
- `nil` for non-existent fields

## Commands Used

```bash
./run_tests.sh  # Run all vybe tests
```

## What's Next

- Consider implementing `:assoc` behavior for mutable updates (if jank user-types support it)
- Could add `:keys` and `:seq` for iteration support
- ComponentRefs (from `get-comp`) still use the old map-based approach - could be unified
