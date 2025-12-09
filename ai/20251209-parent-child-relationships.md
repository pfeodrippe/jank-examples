# Parent/Child Relationships in with-query

## Date: 2025-12-09

## Summary

Added support for parent/child hierarchical relationships in Flecs entities, enabling nested entity definitions via maps and explicit `:vf/child-of` relationships.

## Changes Made

### 1. Added C++ helpers in `src/vybe/flecs.jank` (cpp/raw block)

```cpp
// Create a pair from two entity IDs
ecs_entity_t vybe_ecs_pair(ecs_entity_t first, ecs_entity_t second) {
  return ecs_pair(first, second);
}

// Get EcsChildOf constant
ecs_entity_t vybe_EcsChildOf() {
  return EcsChildOf;
}

// Get children iterator for an entity
jank::runtime::object_ref vybe_children_ids(ecs_world_t* w, ecs_entity_t parent);
```

### 2. Added `path` function in `src/vybe/flecs.jank`

```clojure
(defn path
  "Builds a path string from a sequence of entity names/ids.
   Uses '#id.name' format for numeric parent IDs.
   E.g. (path [:sun :mercury]) => \"sun.mercury\"
        (path [123 :mercury]) => \"#123.mercury\""
  [ks]
  ...)
```

### 3. Added `builtin-entities` map

```clojure
(def EcsChildOf
  "The EcsChildOf builtin relationship entity ID."
  (cpp/vybe_EcsChildOf))

(def builtin-entities
  "Map of builtin Flecs entities."
  {:vf/child-of EcsChildOf})
```

### 4. Updated `eid` function

Now handles:
- `VybeFlecsEntity` user types: extracts `:vf/entity-id` directly
- Vectors as pairs: `[:vf/child-of :parent]` creates a ChildOf pair
- Builtin entity lookup for `:vf/child-of`

Note: Uses `(some? (get e :vf/entity-id))` instead of `(contains? e :vf/entity-id)` because jank user types may not implement `contains?` properly.

### 5. Updated `-add-item-to-entity!` function

Now handles:
- Keyword tags
- Component instances (with `:vt/comp`)
- Vector pairs (e.g., `[:vf/child-of :parent]`)
- Maps for nested children definitions

### 6. Updated `-world-get-entity` function

Now supports vector path lookups: `(get w [:sun :mercury])` looks up `"sun.mercury"`

### 7. Added `children-ids` function

```clojure
(defn children-ids
  "Get all child entity IDs for a given parent entity."
  [world parent-entity-id]
  (cpp/vybe_children_ids (world-ptr world) parent-entity-id))
```

### 8. Added tests in `test/vybe/flecs_test.jank`

- `children-test`: Tests nested children via maps syntax
- `child-of-test`: Tests explicit `:vf/child-of` relationship

## Usage Examples

### Nested children via maps

```clojure
;; Create a hierarchy: sun with children mercury and venus
;; venus has a child moon
(merge w {:sun [:star (Position {:x 1.0 :y 1.0})
                ;; Children are defined as a map
                {:mercury [:planet (Position {:x 1.0 :y 1.0})]
                 :venus [:planet (Position {:x 2.0 :y 2.0})
                         ;; Nested child
                         {:moon [:moon (Position {:x 0.1 :y 0.1})]}]}]})

;; Access parent
(is (contains? w :sun))

;; Access children via path vector
(is (some? (get w [:sun :mercury])))
(is (some? (get w [:sun :venus])))

;; Access nested children
(is (some? (get w [:sun :venus :moon])))
```

### Explicit :vf/child-of relationship

```clojure
;; Create parent first
(assoc w :sun [:star (Position {:x 1.0 :y 1.0})])

;; Create child using :vf/child-of
(assoc w :earth [:planet (Position {:x 3.0 :y 3.0}) [:vf/child-of :sun]])

;; Access via path
(is (some? (get w [:sun :earth])))
```

## Flecs Integration

The implementation uses Flecs' native ChildOf relationship:
- `ecs_pair(EcsChildOf, parent_id)` creates the pair ID
- Child entities are named with path format: `"sun.mercury"` for entity named `:mercury` under `:sun`
- Lookup uses `ecs_lookup` with the full path string

## Test Results

All 37 tests pass:
- vybe.flecs-test: 18 tests, 41 assertions (added children-test, child-of-test)
- vybe.type-test: 19 tests, 83 assertions

## Path Format

| Input | Path String |
|-------|-------------|
| `[:sun :mercury]` | `"sun.mercury"` |
| `[123 :mercury]` | `"#123.mercury"` |
| `[:sun :venus :moon]` | `"sun.venus.moon"` |
