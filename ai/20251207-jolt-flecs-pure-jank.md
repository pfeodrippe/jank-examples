# Jolt and Flecs Functions Converted to Pure jank

**Date**: 2025-12-07

## Summary

Converted all Jolt and Flecs wrapper functions from C++ to pure jank.

## Functions Converted

### Jolt Functions

```clojure
;; Create physics world
(defn create-world []
  (cpp/box (jolt/jolt_world_create)))

;; Destroy physics world
(defn destroy-world! [w]
  (jolt/jolt_world_destroy (cpp/opaque_box_ptr w))
  nil)

;; Create sphere body
(defn create-sphere [w x y z r]
  (jolt/jolt_body_create_sphere
   (cpp/opaque_box_ptr w)
   (cpp/float. x) (cpp/float. y) (cpp/float. z) (cpp/float. r)
   (cpp/bool. true) (cpp/bool. true)))

;; Create floor body
(defn create-floor [w]
  (jolt/jolt_body_create_box
   (cpp/opaque_box_ptr w)
   (cpp/float. 0.0) (cpp/float. -0.5) (cpp/float. 0.0)
   (cpp/float. 50.0) (cpp/float. 0.5) (cpp/float. 50.0)
   (cpp/bool. false) (cpp/bool. false)))
```

### Flecs Functions

```clojure
;; Create Flecs world
(defn flecs-create-world []
  (cpp/box (fl/ecs_mini)))

;; Destroy Flecs world
(defn flecs-destroy-world! [flecs-world]
  (fl/ecs_fini (cpp/to_ecs_world (cpp/opaque_box_ptr flecs-world)))
  nil)
```

## Key Patterns

### 1. Creating opaque_box from C API return value
```clojure
(cpp/box (jolt/jolt_world_create))
(cpp/box (fl/ecs_mini))
```

### 2. Extracting void* from opaque_box
```clojure
(cpp/opaque_box_ptr boxed-value)
```

### 3. Converting jank values to C++ types
```clojure
(cpp/float. x)       ; jank number -> C++ float
(cpp/bool. true)     ; jank boolean -> C++ bool
(cpp/uint32_t. id)   ; jank number -> C++ uint32_t
```

### 4. Pointer type casting (via helper)
```clojure
(cpp/to_ecs_world (cpp/opaque_box_ptr w))  ; void* -> ecs_world_t*
```

## Errors Encountered

### Error 1: "expected real found integer"
**Problem**: Passing integer literal `0` to `cpp/float.`
**Solution**: Use float literal `0.0`
```clojure
;; BAD
(cpp/float. 0)

;; GOOD
(cpp/float. 0.0)
```

### Error 2: "expected integer found boolean"
**Problem**: Passing jank `true`/`false` directly to C++ function expecting `bool`
**Solution**: Use `cpp/bool.` to convert
```clojure
;; BAD
(some-c-function true false)

;; GOOD
(some-c-function (cpp/bool. true) (cpp/bool. false))
```

## C++ Code Removed

- `jolt_create_world()` - replaced by `(cpp/box (jolt/jolt_world_create))`
- `jolt_destroy_world()` - replaced by `(jolt/jolt_world_destroy (cpp/opaque_box_ptr w))`
- `jolt_create_sphere()` - replaced by direct call with type conversions
- `jolt_create_floor()` - replaced by direct call with type conversions
- `flecs_fini_world()` - replaced by `(fl/ecs_fini (cpp/to_ecs_world ...))`
- `g_jolt_world` global - no longer needed

## Files Changed

- `src/my_integrated_demo.jank`:
  - Converted 5 functions from C++ to pure jank
  - Removed C++ wrapper functions
  - Removed unused `g_jolt_world` global
