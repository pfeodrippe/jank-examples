# defn* Macro Implementation

**Date**: 2025-12-07

## Summary

Implemented `defn*` macro in `vybe.flecs` to provide ergonomic syntax for functions that need to return native C++ types.

## Problem

Due to virtual dispatch limitations in jank (JIT functions inherit from `jit_function` with `object_ref call()` method), functions cannot directly return native C++ types. The workaround is to use macros that inline the code at call sites.

## Solution: `defn*` Macro

The `defn*` macro provides a `defn`-like syntax but generates a `defmacro` that wraps the body in `cpp/unbox`:

```clojure
;; Usage:
(defn* ^{:tag "ecs_world_t*"} world-ptr [world]
  world)

;; Expands to:
(defmacro world-ptr [world]
  (list 'cpp/unbox (list 'cpp/type "ecs_world_t*") world))
```

When called, `(world-ptr my-boxed-world)` inlines to:
```clojure
(cpp/unbox (cpp/type "ecs_world_t*") my-boxed-world)
```

## When to Use Each Pattern

1. **`defn*` with `:tag`** - For functions that take a boxed value and need to return it unboxed:
   ```clojure
   (defn* ^{:tag "ecs_world_t*"} world-ptr [world]
     world)
   ```

2. **Regular `defmacro`** - For functions that call C functions returning native types directly:
   ```clojure
   (defmacro iter-next [iter]
     `(fl/ecs_query_next (cpp/& ~iter)))
   ```

3. **Regular `defn`** - For functions that return boxed values (object_ref):
   ```clojure
   (defn ^{:tag "ecs_world_t*"} make-world []
     (cpp/box (fl/ecs_init)))
   ```

## Files Changed

- **`/Users/pfeodrippe/dev/something/src/vybe/flecs.jank`**
  - Added `defn*` macro implementation
  - Converted `world-ptr` from `defmacro` to `defn*`
  - Enabled previously commented-out macros: `query-iter`, `iter-next`, `iter-count`, `iter-entities`, `iter-entity-at`, `with-each`, `field-ptr`, `field-at`

## Commands Run

```bash
# Test the integrated demo
./run_integrated.sh
```

## Implementation Details

The `defn*` macro:
1. Extracts `:tag` metadata from the function name
2. Parses optional docstring and parameters
3. If `:tag` is present, generates a `defmacro` that wraps body in `cpp/unbox`
4. If no `:tag`, falls back to regular `defn`

```clojure
(defmacro defn* [name & body]
  (let [m (meta name)
        tag (:tag m)
        [docstring params forms] (if (string? (first body))
                                   [(first body) (second body) (drop 2 body)]
                                   [nil (first body) (rest body)])]
    (if tag
      `(defmacro ~(with-meta name (dissoc m :tag))
         ~@(when docstring [docstring])
         ~params
         (list (quote cpp/unbox)
               (list (quote cpp/type) ~tag)
               ~@forms))
      `(defn ~name ~@body))))
```

## Next Steps

- The remaining commented-out functions (`query-str`, `register-component`) have a different issue: they use `cpp/value` with dynamic strings from `str`, which requires literal strings. These need a different approach (possibly `cpp/raw`).
