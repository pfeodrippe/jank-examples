# Plan: Make with-query Match Vybe Interface (COMPLETED)

**Date**: 2025-12-08
**Status**: DONE - All tests pass

## Current vybe Interface Analysis

From `/Users/pfeodrippe/dev/vybe/src/vybe/flecs.clj` and `/Users/pfeodrippe/dev/vybe/test/vybe/flecs_test.clj`:

### Usage Examples from vybe tests:

```clojure
;; 1. Simple component + entity
(vf/with-query w [pos Position, e :vf/entity]
  [e pos])

;; 2. Multiple components with :mut modifier
(vf/with-query w [e :vf/entity, pos [:mut Position], speed ImpulseSpeed
                  defense [:mut Defense], capacity [:mut FreightCapacity]]
  ...)

;; 3. Pair/wildcard queries
(vf/with-query w [a A, v [:a :*]]
  [a v])

;; 4. Destructuring
(vf/with-query w [speed ImpulseSpeed, {:keys [x] :as pos} Position, e :vf/entity]
  ...)

;; 5. Datalog variable bindings
(vf/with-query w [_ [Translation '?e]
                  _ [Rotation '?e]
                  e :vf/entity]
  ...)
```

### Key Observations:

1. **NO `:vf/expr` keyword** - Query is built from the bindings themselves
2. **Bindings are pairs**: `symbol spec, symbol spec, ...`
3. **Special keywords**:
   - `:vf/entity` - binds the entity (as a rich entity object)
   - `:vf/eid` - binds entity ID (as integer)
   - `:vf/iter` - binds the iterator
   - `:vf/world` - binds the world
   - `:vf/event` - binds the event (for observers)

4. **Component specs can be**:
   - Symbol: `Position` - component type
   - Vector with modifier: `[:mut Position]` - mutable access
   - Vector pair: `[:a :*]` - pair with wildcard
   - Vector with datalog var: `[Translation '?e]`

## The Problem

In vybe (Clojure), components are VybeComponent objects that know their:
- Name (for query string generation)
- Layout (for field access)
- Size (for memory operations)

In jank, we don't have this infrastructure yet. We're using:
- Raw Flecs C API
- Query strings directly

## Proposed Solution for jank

### Phase 1: String-based queries (minimal change)

Change interface from:
```clojure
;; Current (wrong):
(vf/with-query w [:vf/expr "TestTag", e :vf/entity] ...)

;; To match vybe pattern:
(vf/with-query w [_ "TestTag", e :vf/entity] ...)
```

The `_` is used for components we don't need to bind (like in vybe). The string becomes the query expression.

### Phase 2: Support component symbols (future)

Eventually support:
```clojure
(vf/with-query w [pos Position, e :vf/entity] ...)
```

Where `Position` would be defined via `defcomponent` and registered with Flecs.

## Implementation Plan

### Step 1: Update with-query macro

Change binding parsing:
- Look for string literals as query terms (instead of `:vf/expr`)
- Combine multiple string terms into a single query expression
- `:vf/entity` binds entity ID

```clojure
(defmacro with-query
  [world bindings & body]
  (let [binding-pairs (partition 2 bindings)
        ;; Extract query terms (strings)
        query-terms (->> binding-pairs
                         (map second)
                         (filter string?))
        query-string (str/join ", " query-terms)
        ;; Extract entity binding
        entity-binding (->> binding-pairs
                            (filter #(= :vf/entity (second %)))
                            first
                            first)]
    ...))
```

### Step 2: Update test

```clojure
;; Change from:
(vf/with-query w [:vf/expr "TestTag", e :vf/entity]
  e)

;; To:
(vf/with-query w [_ "TestTag", e :vf/entity]
  e)
```

### Step 3: Support multiple query terms

```clojure
;; Multiple terms become: "Position, Velocity"
(vf/with-query w [_ "Position", _ "Velocity", e :vf/entity]
  e)
```

## Questions to Consider

1. **Should we support `_` as a special ignore symbol?** Yes, like vybe does for unused bindings.

2. **What about component data access?** For now, just entity IDs. Component data requires more infrastructure.

3. **Order of bindings?** In vybe, entity binding can be anywhere. We should support this too.

## Commands Run

```bash
./run_tests.sh
# Output: All tests passed!
```

## Files Modified

1. `/Users/pfeodrippe/dev/something/src/vybe/flecs.jank` - Updated `with-query` macro
2. `/Users/pfeodrippe/dev/something/test/vybe/flecs_test.jank` - Updated test to use new syntax

## Implementation Notes

1. Changed interface from `[:vf/expr "Query", e :vf/entity]` to `[_ "Query", e :vf/entity]`
2. Used `(apply str (interpose ", " query-terms))` instead of `clojure.string/join` (not available in jank)
3. String values in bindings become query terms
4. `:vf/entity` binds entity ID
5. Multiple string terms are joined with ", " to form the full query expression
