# with-system Macro Implementation

## Date: 2025-12-10

## Task
Create a `with-system` macro similar to `with-query` that defines a Flecs system.

## What was learned

### Flecs System API
- Systems use `ecs_system_desc_t` with `query.expr` for query string and `callback` for the action
- Systems are registered via `ecs_system_init()`
- Systems run during `ecs_progress()` or manually via `ecs_run()`
- The callback receives `ecs_iter_t*` for iterating over matched entities

### vybe's with-system Interface
From `/Users/pfeodrippe/dev/vybe/src/vybe/flecs.clj`:
- Requires `:vf/name` binding for system name
- Same component/tag binding syntax as `with-query`
- Body becomes the system callback
- Returns system entity ID
- System doesn't run immediately - runs on `progress` or `system-run`

## Implementation

### 1. C++ Callback Infrastructure (`src/vybe/flecs.jank`)
Added system callback dispatcher in cpp/raw:
- `vybe_system_callbacks` - global map of system ID -> jank callback
- `vybe_register_system_callback()` - register jank callback
- `vybe_unregister_system_callback()` - cleanup
- `vybe_system_dispatcher()` - C callback that dispatches to jank
- `vybe_create_system()` - creates system with dispatcher callback

### 2. Iterator Helpers
- `vybe_iter_count()` - get entity count in current table
- `vybe_iter_entity()` - get entity ID at index
- `vybe_iter_field_ptr()` - get component data pointer

### 3. Shared Parsing Logic
Refactored to share code between `with-query` and `with-system`:
- `parse-query-bindings` - extracts components, tags, entity binding from bindings
- `gen-ensure-tags-and-comps` - generates registration code
- `gen-entity-body-bindings` - generates component bindings in loop body

### 4. with-system Macro
```clojure
(vf/with-system world [:vf/name :my-movement-system
                       p Position
                       v Velocity
                       e :vf/entity]
  (merge p {:x (+ (:x p) (:dx v))
            :y (+ (:y p) (:dy v))}))
```

- Parses bindings using shared `parse-query-bindings`
- Creates system via `create-system`
- Registers callback that loops over entities and binds components
- Returns system entity ID

## Tests Added
In `test/vybe/flecs_test.jank`:
1. `with-system-basic-test` - system registration and `system-run`
2. `with-system-with-velocity-test` - component updates (movement system)
3. `with-system-progress-test` - system runs during `progress`
4. `with-system-with-tag-test` - filtering by tags

## Commands Used
- Read files: `src/vybe/flecs.jank`, `test/vybe/flecs_test.jank`, vybe project files
- Edit files: Added C++ helpers and macros to `src/vybe/flecs.jank`, added tests
- Grep: Searched for system APIs in Flecs headers

## Key Debugging Lessons

1. **`partition 2` returns lazy seqs, not vectors** - Can't use `(into {} (partition 2 bindings))` directly, need `(into {} (map vec (partition 2 bindings)))`

2. **Can't use `recur` through `try`** - jank's `testing` macro uses try, so loops with `recur` fail. Use `mapv` instead.

3. **`cpp/type` only takes string literals** - Can't pass runtime values. Use C++ helper functions instead.

4. **Passing pointers between C++ callbacks and jank** - Use `int64_t` integers:
   - C++: `reinterpret_cast<int64_t>(ptr)` then box as `jank::runtime::obj::integer`
   - jank: Pass integer directly to helper functions that cast back

5. **`apply_to` takes (fn, args-vector)** - Not separate arguments like `(fn, arg1, arg2)`

## System Redefinition

Added support for redefining systems with the same name:

```cpp
// In vybe_create_system():
ecs_entity_t existing = ecs_lookup(w, name);
if (existing != 0 && vybe_is_system(w, existing)) {
  vybe_delete_system(w, existing);  // Unregisters callback + deletes entity
}
```

This allows:
```clojure
;; First definition
(vf/with-system w [:vf/name :my-system, ...] body1)

;; Redefine - old one is deleted, new one created
(vf/with-system w [:vf/name :my-system, ...] body2)
```

## defsystem Macro

Added `defsystem` macro similar to vybe's implementation:

```clojure
(vf/defsystem gravity-system w
  [p Position
   e :vf/entity]
  ;; Apply gravity - decrease y by 1
  (merge p {:y (- (:y p) 1.0)}))

;; Register system by calling the function
(gravity-system w)

;; Run it
(vf/system-run w :vybe.flecs-test/gravity-system)
```

The macro:
1. Takes a name, world parameter, bindings, and body
2. Generates a `defn` that wraps `with-system`
3. Auto-generates system name as `:<ns>/<name>` (e.g., `:vybe.flecs-test/gravity-system`)
4. Supports redefinition - calling the function again replaces the system

**IMPORTANT**: `defsystem` must be used at top-level only (not inside let/do blocks) because it expands to `defn`.

### Tests Added for defsystem
- `defsystem-test` - basic system registration via function call (top-level definition)

## Final Test Results

All 23 tests pass:
- 4 with-system tests (including redefinition in `with-system-with-velocity-test`)
- 1 defsystem test
- 18 other flecs tests

```
Ran 23 tests containing 61 assertions.
0 failures, 0 errors.
```

## What's Next
- Consider adding more options like `:vf/phase`, `:vf/always`, `:vf/disabled`
- Add `with-observer` macro for Flecs observers
