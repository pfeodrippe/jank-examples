# Session: with-query Component Binding

**Date**: 2025-12-08
**Status**: COMPLETED - All 28 tests pass (9 flecs + 19 type)

## What Was Implemented

### 1. Component-Based with-query Syntax
Updated `with-query` macro to accept component constructors directly:

```clojure
;; Before (string-based):
(vf/with-query w [_ "vybe_flecs_test__Position"
                  _ "vybe_flecs_test__Velocity"
                  e :vf/entity]
  (let [pos (vt/get-comp w e Position)
        vel (vt/get-comp w e Velocity)]
    ...))

;; After (component-based):
(vf/with-query w [p Position
                  v Velocity
                  e :vf/entity]
  (vt/merge! p {:x (+ (:x p) (:dx v))
                :y (+ (:y p) (:dy v))}))
```

This is the jank equivalent of:
```cpp
q.each([](flecs::entity e, Position& p, const Velocity& v) {
    p.x += v.x;
    p.y += v.y;
});
```

### 2. Runtime Query String Support
Added C++ helper to create queries from runtime strings:

```cpp
jank::runtime::object_ref vybe_query_entities_str(ecs_world_t* w, const char* query_str) {
  ecs_query_desc_t desc = {};
  desc.expr = query_str;
  ecs_query_t* q = ecs_query_init(w, &desc);
  // ... iterate and return entities
}
```

### 3. Query String Builder
Added `build-query-string` function to vybe.type:

```clojure
(defn build-query-string
  "Build a query string from a list of specs (strings or components)."
  [specs]
  (apply str (interpose ", " (map (fn [spec]
                                     (if (string? spec)
                                       spec
                                       (comp-name spec)))
                                   specs))))
```

## Key Technical Details

### How with-query Works Now

1. **Macro expansion** identifies component vs entity bindings
2. **Runtime**: `build-query-string` converts components to mangled names
3. **Runtime**: `query-entities-runtime` creates query and returns matching entities
4. **Body execution**: Each component symbol is bound via `vt/get-comp`

```clojure
;; Macro generates:
(let [entities# (query-entities-runtime w
                  (vybe.type/build-query-string [Position Velocity]))]
  (mapv (fn [e]
          (let [p (vybe.type/get-comp w e Position)
                v (vybe.type/get-comp w e Velocity)]
            (vt/merge! p {:x (+ (:x p) (:dx v))})))
        entities#))
```

### Avoiding Circular Dependencies

- `vybe.type` requires `vybe.flecs`
- `vybe.flecs/with-query` generates code that calls `vybe.type/build-query-string` and `vybe.type/get-comp`
- These are **runtime calls** in the generated code, not compile-time dependencies
- No circular require needed - the generated code runs in user's namespace which has both required

## Commands Run

```bash
./run_tests.sh
# Testing vybe.flecs-test - 9 tests, 12 assertions
# Testing vybe.type-test - 19 tests, 64 assertions
# All tests passed!
```

## Files Modified

- `src/vybe/flecs.jank`:
  - Added `vybe_query_entities_str` C++ helper for runtime query strings
  - Added `query-entities-runtime` function
  - Updated `with-query` macro to accept components directly

- `src/vybe/type.jank`:
  - Added `comp-name` function
  - Added `build-query-string` function

- `test/vybe/flecs_test.jank`:
  - Added vybe.type require
  - Defined Position and Velocity components
  - Updated `with-query-test` to use component-based syntax

- `test/vybe/type_test.jank`:
  - Fixed typo in test (42.8 -> 42.5)

## API Summary

| Syntax | Description |
|--------|-------------|
| `[p Position, ...]` | Binds component data to `p` |
| `[_ "StringTerm", ...]` | Query term without binding |
| `[e :vf/entity]` | Binds entity ID to `e` |

## Example Usage

```clojure
;; Define components
(vt/defcomp Position [[:x :float] [:y :float]])
(vt/defcomp Velocity [[:dx :float] [:dy :float]])

;; Register with world
(vt/register-comp! world Position)
(vt/register-comp! world Velocity)

;; Add to entities
(vt/add-comp! world e1 Position {:x 0.0 :y 0.0})
(vt/add-comp! world e1 Velocity {:dx 1.0 :dy 2.0})

;; Query and update
(vf/with-query world [p Position, v Velocity, e :vf/entity]
  (vt/merge! p {:x (+ (:x p) (:dx v))
                :y (+ (:y p) (:dy v))}))
```
