# Remove Circular Dependencies Between vybe.type and vybe.flecs

## Date: 2025-12-09

## Summary

Removed implicit bidirectional dependencies by eliminating all `@(resolve ...)` calls and establishing a clean one-way dependency: `vybe.flecs` depends on `vybe.type`.

## Problem

The codebase had circular dependencies:
- `vybe.type` required `vybe.flecs` (for `vf/world-ptr`, `vf/make-world`, etc.)
- `vybe.flecs` used `@(resolve 'vybe.type/...)` to call back to type functions

This created implicit bidirectional dependencies which is bad practice.

## Solution

### Architecture Change

**Before:**
```
vybe.type ←→ vybe.flecs (bidirectional via resolve)
```

**After:**
```
vybe.type ← vybe.flecs (clean one-way dependency)
```

### Changes to vybe.type

1. **Removed `[vybe.flecs :as vf]` require**

2. **Added local helper functions** (duplicated from flecs.jank):
   ```clojure
   (defn- -get-world-box [world] ...)
   (defmacro -world-ptr [world] ...)
   (defn- -make-world [] ...)
   ```

3. **Added local component name registry**:
   ```clojure
   (def ^:private -comp-name-registry (atom {}))
   (defn register-descriptor-by-name [desc] ...)
   (defn resolve-descriptor-by-name [name-str] ...)
   ```

4. **Added C++ helper** (with unique name to avoid conflicts):
   ```cpp
   int64_t vybe_type_ptr_to_int64(ecs_world_t* ptr) {
     return reinterpret_cast<int64_t>(ptr);
   }
   ```

5. **Replaced all `vf/` calls**:
   - `vf/world-ptr` → `-world-ptr`
   - `vf/make-world` → `-make-world`
   - `vf/register-descriptor-by-name` → `register-descriptor-by-name`

### Changes to vybe.flecs

1. **Added `[vybe.type :as vt]` require**

2. **Replaced all `@(resolve 'vybe.type/...)` with direct calls**:
   - `@(resolve 'vybe.type/get-comp)` → `vt/get-comp`
   - `@(resolve 'vybe.type/add-comp!)` → `vt/add-comp!`
   - `@(resolve 'vybe.type/resolve-descriptor)` → `vt/resolve-descriptor`

3. **Removed duplicate registry code** (now in type.jank):
   - Removed `comp-name-registry` atom
   - Removed `register-descriptor-by-name` function
   - Removed `resolve-descriptor-by-name` function
   - Removed `register-comp-name!` function

4. **Updated internal calls** to use `vt/resolve-descriptor-by-name`

## Files Modified

- `src/vybe/type.jank` - Made independent, added local helpers
- `src/vybe/flecs.jank` - Now properly depends on vybe.type

## Test Results

All tests pass:
- 16 flecs tests (30 assertions)
- 19 type tests (83 assertions)
- Total: 35 tests, 113 assertions

## Commands

```bash
./run_tests.sh
```

## Additional Change: Renamed make-world-map to make-world

Renamed `make-world-map` to `make-world` for cleaner API:
- `make-world` now returns the VybeFlecsWorld user type (with map interface)
- Old simple `make-world` renamed to `-make-world` (internal use only)
- Updated all tests to use `vf/make-world`

## Bug Fix: Stale JIT Function References in User Type Factories

The user type factories for VybeFlecsWorld and VybeFlecsEntity were cached in atoms:
```clojure
(def ^:private -vybe-flecs-entity-factory (atom nil))
(def ^:private -vybe-flecs-world-factory (atom nil))
```

This caused intermittent "invalid call with N args" errors when running multiple tests because:
- jank JIT-compiles functions at runtime
- Cached factories held references to old JIT function pointers
- When jank reloaded namespaces, the cached function pointers became invalid

**Fix**: Removed factory caching - create fresh factories each time:
```clojure
(defn- -create-vybe-flecs-entity [world w-box entity-id]
  ;; Create fresh factory each time to avoid stale JIT function references
  (let [factory (-create-vybe-flecs-entity-user-type)]
    (factory {...})))
```

## Auto-Registration of Components

Modified `comp-id` to auto-register components if not already registered:
```clojure
(defn comp-id [world comp]
  ...
  ;; Not found - auto-register the component
  (register-comp! world comp))
```

Now users can simply do:
```clojure
(def w (vf/make-world))
(assoc w :bob [(Position {:x 10.0 :y 20.0})])  ;; auto-registers Position
```

No need to call `register-comp!` manually with the map interface.

## Notes

- The component name registry is now in `vybe.type` and is the single source of truth
- `type.jank` duplicates some simple helper functions to avoid the dependency
- C++ helper function names are namespaced (`vybe_type_ptr_to_int64`) to avoid ODR violations
- **Do NOT cache user type factories** - always create fresh to avoid stale JIT function references
- Components are auto-registered on first use with a world
