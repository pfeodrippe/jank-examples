# cpp/.at for Direct Vector/Container Access

**Date**: 2025-12-05

## What Was Learned

### Using cpp/.at for std::vector Element Access

Discovered that jank supports `cpp/.at` for accessing vector/container elements by index, similar to the `[]` subscript operator in C++. This eliminates the need for C++ accessor helper functions.

### Key Pattern

```clojure
;; Access vector element at index
(let [entity (cpp/.at (cpp/get_entities) (cpp/size_t. idx))]
  ;; Access struct fields directly
  (cpp/.-jolt_id entity)
  (cpp/.-radius entity)
  (cpp/.-r entity))
```

### Important Requirements

1. **Index type must be `size_t`**: Use `(cpp/size_t. idx)` to convert jank integers to C++ `size_t`
2. **Works with any container**: Supports std::vector, std::array, or any container with `.at()` method
3. **Returns a reference**: The result allows direct field access with `cpp/.-field`

### What Doesn't Work

- `cpp/[]` - Not a valid syntax, jank parses `[]` as empty brackets
- Passing raw jank integers without `cpp/size_t.` - Causes ambiguous overload error

## Changes Made

### 1. Updated get-entity-color to use cpp/.at
**File**: `src/my_integrated_demo.jank`

```clojure
;; Before: Using C++ helper functions
(defn get-entity-color [idx]
  [(cpp/entity_r idx) (cpp/entity_g idx) (cpp/entity_b idx)])

;; After: Direct vector access with cpp/.at
(defn get-entity-color [idx]
  (let [entity (cpp/.at (cpp/get_entities) (cpp/size_t. idx))]
    [(cpp/.-r entity) (cpp/.-g entity) (cpp/.-b entity)]))
```

### 2. Updated draw-entity! to use cpp/.at
**File**: `src/my_integrated_demo.jank`

```clojure
(defn draw-entity! [jolt-world idx]
  (let [entity (cpp/.at (cpp/get_entities) (cpp/size_t. idx))
        jolt-id (cpp/.-jolt_id entity)
        radius (cpp/.-radius entity)
        ;; ... rest of function
        ]))
```

### 3. Removed C++ accessor helper functions
**File**: `src/my_integrated_demo.jank` (cpp/raw block)

Removed these functions (no longer needed):
- `entity_jolt_id(int64_t idx)`
- `entity_radius(int64_t idx)`
- `entity_r(int64_t idx)`
- `entity_g(int64_t idx)`
- `entity_b(int64_t idx)`

### 4. Updated native resources guide
**File**: `ai/20251202-native-resources-guide.md`

Added new section "Container Access with cpp/.at" documenting:
- Syntax and usage
- Why `cpp/size_t.` is required
- Example patterns
- Comparison with old C++ helper approach

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## Summary

| Old Pattern | New Pattern |
|-------------|-------------|
| `(cpp/entity_jolt_id idx)` | `(cpp/.-jolt_id (cpp/.at (cpp/get_entities) (cpp/size_t. idx)))` |
| `(cpp/entity_radius idx)` | `(cpp/.-radius (cpp/.at (cpp/get_entities) (cpp/size_t. idx)))` |
| Required C++ wrapper functions | Direct pure jank access |

## Benefits

1. **Less cpp/raw code**: Removed 5 C++ accessor functions
2. **More idiomatic**: Uses jank's built-in C++ interop
3. **Single access point**: Get element once, access multiple fields
4. **Cleaner code**: No need for separate getter functions per field

## What's Next

- Look for other cpp/raw helper functions that could be replaced with direct access
- Consider if EntityPos struct could use similar pattern (currently uses global state for out parameters)
- Explore if there are other container methods available via cpp/.method
