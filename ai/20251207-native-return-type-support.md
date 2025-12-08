# Native Return Type Support Investigation

**Date**: 2025-12-07

## Summary

Investigated support for jank functions to return native C++ types directly when they have `:tag` metadata. Found an architectural limitation that prevents this.

## Problem

Functions like `world-ptr` in `vybe/flecs.jank` that want to return a native pointer type:

```clojure
(defn ^{:tag "ecs_world_t*"} world-ptr
  [world]
  (cpp/unbox (cpp/type "ecs_world_t*") world))
```

Fail with:
```
This function is returning a native object of type 'ecs_world_t *', which is not convertible to a jank runtime object.
```

## Root Cause: Virtual Dispatch Limitation

JIT-compiled jank functions inherit from `jit_function` which has a virtual `call` method:

```cpp
// In jit_function.hpp
object_ref call(object_ref) override;
```

Changing the return type to a native type (e.g., `ecs_world_t *`) breaks virtual dispatch because:
1. C++ doesn't support covariant return types for unrelated types
2. All jank function calls go through the virtual `call` method
3. Callers expect `object_ref` to be returned

## Attempted Solution

Modified the analyzer to allow native return types and the codegen to generate native return types:
- Added `current_native_return_type` tracking in `processor.hpp`
- Modified `analyze_fn_arity` to skip implicit conversion when types match
- Modified codegen to output native return type instead of `object_ref`

**Result**: Compilation error because the generated `call` method can't override the base class virtual method with a different return type.

## Working Solution: Use Macros

For functions that need to return native types, use `defmacro` instead of `defn`:

```clojure
(defmacro world-ptr
  "Extract ecs_world_t* from boxed world."
  [world]
  `(cpp/unbox (cpp/type "ecs_world_t*") ~world))
```

The macro inlines the code at call sites, avoiding the virtual dispatch limitation.

## Future Improvement: `defn*` Macro

A `defn*` macro could automate generating macros based on `:tag` metadata:

```clojure
;; Usage:
(defn* ^{:tag "ecs_world_t*"} world-ptr [world]
  (cpp/unbox (cpp/type "ecs_world_t*") world))

;; Expands to:
(defmacro world-ptr [world]
  `(cpp/unbox (cpp/type "ecs_world_t*") ~world))
```

This would provide the ergonomics of `defn` with `:tag` while generating macros under the hood.

## Files Changed

- **`/Users/pfeodrippe/dev/something/src/vybe/flecs.jank`**
  - Changed `world-ptr` from `defn` to `defmacro`
  - Commented out several functions that have similar issues

- **Reverted jank compiler changes** - the architectural limitation makes this approach non-viable

## Tag Type Inference (Still Working)

The `:tag` metadata is still useful for type inference with `cpp/unbox`:

```clojure
;; Function returns a boxed pointer
(defn ^{:tag "ecs_world_t*"} make-world []
  (cpp/box (cpp/ecs_init)))

;; At call site, cpp/unbox infers the type from the tag
(let [world-ptr (cpp/unbox (make-world))]
  (cpp/ecs_new world-ptr))
```

This pattern works because the function returns a boxed value (`object_ref`), and the tag tells callers what type to unbox to.
