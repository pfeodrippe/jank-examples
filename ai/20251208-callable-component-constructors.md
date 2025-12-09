# Session: Callable Component Constructors

**Date**: 2025-12-08
**Status**: COMPLETED - All 19 tests pass (9 flecs + 19 type, 64 assertions)

## What Was Implemented

### 1. Callable Component Constructors
`defcomp` now creates callable constructor functions:

```clojure
(vt/defcomp Position [[:x :float] [:y :float]])

;; Create standalone instances
(def pos-1 (Position {:x 42.5 :y 99.25}))  ; map form
(def pos-2 (Position [42.5 99.25]))         ; vector form
```

### 2. Native Memory-Backed Instances
Instances are backed by native C memory (pointer storage):

```cpp
void* vybe_alloc_comp(int32_t size) {
  return calloc(1, size);  // Zero-initialized
}
void vybe_free_comp(void* ptr) {
  if (ptr) free(ptr);
}
```

### 3. Atom Wrapper for Mutation Visibility
To enable `(:x @pos-1)` after `merge!`, instances are wrapped in atoms:

```clojure
;; Update instance
(vt/merge! pos-1 {:x 99.0})

;; Read with @ dereference
(:x @pos-1)   ;; => 99.0
(:y @pos-1)   ;; => 99.25
```

### 4. resolve-descriptor Pattern
All functions now handle both descriptor maps and constructor functions:

```clojure
(defn resolve-descriptor
  [comp]
  (if (map? comp)
    comp  ; Already a descriptor
    (get @comp-registry comp)))  ; Look up constructor in registry

;; Usage in functions:
(defn register-comp! [world comp]
  (let [desc (resolve-descriptor comp)]
    ...))
```

## Key Technical Details

### Instance Creation Flow
```clojure
(defn- create-standalone-instance
  [comp values]
  (when-not (get-comp-meta comp)
    (init-comp-meta! comp))
  (let [size (comp-size comp)
        ptr (cpp/vybe_alloc_comp size)
        ptr-box (cpp/box ptr)
        value-map (if (vector? values)
                    (zipmap (map keyword-field-names) values)
                    values)]
    ;; Write values to native memory
    (doseq [[field-kw value] value-map]
      (write-field-to-ptr! ptr-box field-type offset value))
    ;; Return atom wrapping pointer-backed map
    (atom (merge value-map {:vt/comp comp :vt/ptr-box ptr-box}))))
```

### merge! Dual Behavior
Handles both standalone instances and ComponentRefs:

```clojure
(defn merge!
  ([instance-or-ref values]
   (if (and (not (map? instance-or-ref))
            (contains? (deref instance-or-ref) :vt/ptr-box))
     ;; Standalone: write to native memory + update atom
     (let [data @instance-or-ref
           comp (:vt/comp data)
           ptr-box (:vt/ptr-box data)]
       (doseq [[field-kw value] values]
         (write-field-to-ptr! ptr-box field-type offset value))
       (swap! instance-or-ref merge values)
       instance-or-ref)
     ;; ComponentRef: use world-based set-comp!
     ...)))
```

## Errors Fixed

1. **`clojure.lang.Atom` not found in jank** - Used `(not (map? x))` pattern instead
2. **Forward reference to `standalone-instance?`** - Inlined check in `merge!`
3. **Forward reference to `is-f32-type?`** - Used `is-float-type?` (defined earlier)
4. **Forward reference to `comp-registry`** - Moved definition to line 387
5. **`(:name Position)` returning nil** - Updated functions to use `resolve-descriptor`

## Commands Run

```bash
./run_tests.sh
# Testing vybe.flecs-test - 9 tests, 10 assertions
# Testing vybe.type-test - 19 tests, 64 assertions
# All tests passed!
```

## Files Modified

- `src/vybe/type.jank`:
  - Added C++ memory allocation helpers (`vybe_alloc_comp`, `vybe_free_comp`)
  - Moved `comp-registry` definition early (line 387)
  - Added `resolve-descriptor` function
  - Updated `create-standalone-instance` to use native memory + atom
  - Updated `merge!` to handle standalone instances
  - Updated all descriptor-using functions to call `resolve-descriptor`

- `test/vybe/type_test.jank`:
  - Updated `comp-test` to use `@` for dereferencing
  - Updated `defcomp-test` to use `(vt/comp-descriptor Position)`

## API Summary

| Usage | Description |
|-------|-------------|
| `(Position {:x 1.0 :y 2.0})` | Create standalone instance with map |
| `(Position [1.0 2.0])` | Create standalone instance with vector |
| `(vt/merge! pos {:x 99.0})` | Update instance in-place |
| `(:x @pos)` | Read field value (requires `@` deref) |
| `(vt/comp-descriptor Position)` | Get descriptor map from constructor |

## Future Improvements

1. Eliminate atom wrapper if jank supports custom ILookup (allow `(:x pos)` without `@`)
2. Add destructor/finalizer for automatic memory cleanup
3. Consider direct Flecs world integration for standalone instances
