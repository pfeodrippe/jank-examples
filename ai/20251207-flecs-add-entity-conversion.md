# Converting flecs_add_entity to jank

**Date**: 2025-12-07

## Summary

Converted `flecs_add_entity` C++ function to pure jank.

## Before (C++)

```cpp
inline void flecs_add_entity(jank::runtime::object_ref flecs_world_box, int64_t jolt_id, double radius, int64_t r, int64_t g, int64_t b) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(flecs_world_box);
    ecs_world_t* world = static_cast<ecs_world_t*>(o->data.data);
    uint64_t flecs_id = ecs_new(world);

    Entity e;
    e.flecs_id = flecs_id;
    e.jolt_id = (uint32_t)jolt_id;
    e.radius = (float)radius;
    e.r = (uint8_t)r;
    e.g = (uint8_t)g;
    e.b = (uint8_t)b;
    get_entities().push_back(e);
}
```

## After (jank)

```clojure
(defn add-entity!
  "Add entity to Flecs world and entity storage. Pure jank!"
  [flecs-world jolt-id radius cr cg cb]
  (let [world-ptr (cpp/to_ecs_world (cpp/opaque_box_ptr flecs-world))
        flecs-id (fl/ecs_new world-ptr)
        entities (cpp/get_entities)
        e (cpp/value "Entity{}")]
    (cpp/= (cpp/.-flecs_id e) flecs-id)
    (cpp/= (cpp/.-jolt_id e) (cpp/uint32_t. jolt-id))
    (cpp/= (cpp/.-radius e) (cpp/float. radius))
    (cpp/= (cpp/.-r e) (cpp/uint8_t. cr))
    (cpp/= (cpp/.-g e) (cpp/uint8_t. cg))
    (cpp/= (cpp/.-b e) (cpp/uint8_t. cb))
    (cpp/.push_back entities e)
    nil))
```

## Key Patterns

### 1. Pointer casting helper (jank doesn't have static_cast)

Since jank doesn't have `cpp/static_cast`, create a C++ helper:
```cpp
inline ecs_world_t* to_ecs_world(void* p) { return static_cast<ecs_world_t*>(p); }
```

Then use it in jank:
```clojure
(cpp/to_ecs_world (cpp/opaque_box_ptr flecs-world))
```

### 2. Creating a C++ struct with default initialization

```clojure
(cpp/value "Entity{}")  ; Creates a default-initialized Entity struct
```

### 3. Setting struct fields

```clojure
(cpp/= (cpp/.-field_name struct) value)
```

### 4. Calling vector methods

```clojure
(cpp/.push_back vector-ref element)
```

### 5. Type conversions for struct fields

```clojure
(cpp/uint32_t. jank-value)  ; jank -> native uint32_t
(cpp/float. jank-value)      ; jank -> native float
(cpp/uint8_t. jank-value)    ; jank -> native uint8_t
```

## What Didn't Work

- `cpp/cast` - doesn't work for void* to ecs_world_t*
- `cpp/static_cast` - doesn't exist
- `cpp/reinterpret_cast` - doesn't exist
- `cpp/let` - doesn't exist

## Solution for Pointer Casting

When you need to cast pointer types that `cpp/cast` doesn't support, create a small C++ helper function in the `cpp/raw` block:

```cpp
inline TargetType* to_target_type(void* p) { return static_cast<TargetType*>(p); }
```

Then call it from jank:
```clojure
(cpp/to_target_type (cpp/opaque_box_ptr boxed-value))
```

## Files Changed

- `src/my_integrated_demo.jank`:
  - Added `to_ecs_world` helper function
  - Replaced `flecs_add_entity` C++ with pure jank `add-entity!`
