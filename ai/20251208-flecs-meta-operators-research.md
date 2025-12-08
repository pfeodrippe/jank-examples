# Session: Flecs Meta Operators Research

**Date**: 2025-12-08
**Status**: COMPLETED - Research findings documented, all 24 tests pass (9 flecs + 15 type)

## Research Request

User asked: "Research the flecs meta operators in flecs to see what's available, I'm sure it makes metadata about the type (e.g. offset, fields etc)"

## Key Findings

### Core Type Metadata Components

Flecs stores reflection data as entities with components. A type entity has:

1. **EcsComponent** - Core component info:
   ```c
   typedef struct EcsComponent {
       ecs_size_t size;       // Component size in bytes
       ecs_size_t alignment;  // Component alignment
   } EcsComponent;
   ```

2. **EcsType** - Type classification:
   ```c
   typedef struct EcsType {
       ecs_type_kind_t kind;  // EcsPrimitiveType, EcsStructType, EcsEnumType, etc.
       bool existing;         // Was type pre-existing or generated
       bool partial;          // Is reflection data partial
   } EcsType;
   ```

3. **EcsStruct** - Struct reflection data:
   ```c
   typedef struct EcsStruct {
       ecs_vec_t members;     // vector<ecs_member_t>
   } EcsStruct;
   ```

### Member Metadata (THE KEY DISCOVERY)

**EcsMember** - Added to member entities:
```c
typedef struct EcsMember {
    ecs_entity_t type;     // Member type entity
    int32_t count;         // Element count (for inline arrays, 0 otherwise)
    ecs_entity_t unit;     // Optional unit (e.g., meters, seconds)
    int32_t offset;        // BYTE OFFSET OF FIELD IN STRUCT
    bool use_offset;       // If offset was explicitly specified
} EcsMember;
```

**ecs_member_t** - Element type in EcsStruct.members vector:
```c
typedef struct ecs_member_t {
    const char *name;      // Field name
    ecs_entity_t type;     // Member type entity
    int32_t count;         // Element count for inline arrays
    int32_t offset;        // BYTE OFFSET
    ecs_entity_t unit;     // Unit
    bool use_offset;
    ecs_member_value_range_t range;         // Valid value range
    ecs_member_value_range_t error_range;   // Error range
    ecs_member_value_range_t warning_range; // Warning range
    ecs_size_t size;       // MEMBER SIZE IN BYTES
    ecs_entity_t member;   // Member entity
} ecs_member_t;
```

### Type Info Structure

**ecs_type_info_t** - Returned by `ecs_get_type_info()`:
```c
struct ecs_type_info_t {
    ecs_size_t size;         // Total size of type
    ecs_size_t alignment;    // Alignment requirement
    ecs_type_hooks_t hooks;  // Lifecycle hooks (ctor, dtor, copy, move)
    ecs_entity_t component;  // Component entity
    const char *name;        // Type name
};
```

### How to Access Member Metadata

```c
// Get the EcsStruct component from type entity
const EcsStruct* st = ecs_get(world, comp_entity, EcsStruct);

// Get member count
int32_t count = ecs_vec_count(&st->members);

// Access individual members
for (int i = 0; i < count; i++) {
    ecs_member_t* m = ecs_vec_get_t(&st->members, ecs_member_t, i);

    // Now you have:
    // m->name   - field name ("x", "y", etc.)
    // m->type   - type entity (ecs_id(ecs_f32_t), etc.)
    // m->offset - byte offset in struct
    // m->size   - size of member
}
```

### Serializer Opcodes (for efficient iteration)

**EcsTypeSerializer** component contains flattened ops:
```c
typedef struct ecs_meta_op_t {
    ecs_meta_op_kind_t kind;       // EcsOpF32, EcsOpI32, EcsOpPushStruct, etc.
    ecs_size_t offset;             // Offset of field
    const char *name;              // Field name
    ecs_size_t elem_size;          // Element size
    ecs_entity_t type;             // Type entity
    const ecs_type_info_t *type_info;
} ecs_meta_op_t;
```

### Meta Cursor API (what we already use)

- `ecs_meta_cursor(world, type, ptr)` - Create cursor
- `ecs_meta_push(&cur)` / `ecs_meta_pop(&cur)` - Navigate scope
- `ecs_meta_member(&cur, name)` - Move to member
- `ecs_meta_next(&cur)` - Move to next member
- `ecs_meta_get_ptr(&cur)` - Get pointer to current field
- `ecs_meta_get_type(&cur)` - Get type of current field
- `ecs_meta_get_member(&cur)` - Get member name
- `ecs_meta_set_float/int/uint/bool()` - Set values
- `ecs_meta_get_float/int/uint/bool()` - Get values

## What This Enables

### 1. Direct Memory Access (O(1) instead of cursor iteration)
With offset information, we can do:
```c
void* base_ptr = ecs_get_mut_id(world, entity, comp);
void* field_ptr = (char*)base_ptr + member_offset;
*(float*)field_ptr = 42.0f;  // Direct write!
```

### 2. Field Introspection
```clojure
;; Get all fields of a component
(comp-fields Position)
;; => [{:name "x" :type :float :offset 0 :size 4}
;;     {:name "y" :type :float :offset 4 :size 4}]
```

### 3. Component Introspection
```clojure
(comp-size Position)      ;; => 8
(comp-alignment Position) ;; => 4
```

### 4. Value Validation
Members can have range constraints:
```c
ecs_member_value_range_t range;  // min/max values
ecs_member_value_range_t warning_range;
ecs_member_value_range_t error_range;
```

## Potential Optimizations for vybe.type

Currently we use cursor-based access:
```c
ecs_meta_cursor_t cur = ecs_meta_cursor(world, comp, ptr);
ecs_meta_push(&cur);
ecs_meta_member(&cur, "x");
ecs_meta_set_float(&cur, 10.0);  // Multiple function calls per field
```

With offset metadata, we could:
```c
// Cache offsets at registration time
int32_t x_offset = get_member_offset(world, comp, "x");

// Direct access at runtime
*(float*)((char*)ptr + x_offset) = 10.0f;  // Single memory write!
```

This could significantly speed up high-frequency component updates.

## Files Researched

- `vendor/flecs/include/flecs/addons/meta.h` - Main meta API header (1307 lines)
- `vendor/flecs/include/flecs.h` - Core flecs header (EcsComponent, ecs_type_info_t)
- `vendor/flecs/test/script/src/Template.c` - Example usage of struct member access

## Commands Run

```bash
./run_tests.sh
# Testing vybe.flecs-test - 9 tests, 10 assertions
# Testing vybe.type-test - 15 tests, 50 assertions
# All tests passed!
```

## Work Done This Session

1. Researched Flecs meta API in `vendor/flecs/include/flecs/addons/meta.h`
2. Fixed 6 failing tests - updated to handle ComponentRef structure with metadata keys
3. Added 2 new tests:
   - `comp-ref-test` - Verifies `get-comp` returns ComponentRef with metadata
   - `merge!-test` - Tests updating component without world reference

## Files Modified

- `test/vybe/type_test.jank` - Updated tests, added ComponentRef and merge! tests

## Next Steps

1. Consider implementing `comp-fields` function to expose field metadata to jank
2. Consider caching field offsets for direct memory access optimization
3. Add support for nested structs if needed
4. Could expose member units for UI purposes (progress bars, etc.)
