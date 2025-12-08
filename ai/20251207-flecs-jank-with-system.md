# Flecs with-system Implementation for jank

**Date**: 2024-12-07

## What I Learned

### Clojure `with-system` Macro

The original Clojure `with-system` macro in vybe.flecs:
1. Takes a world, bindings (like `[:vf/name :my-system, pos Position, e :vf/entity]`), and body
2. Calls `-system` which:
   - Parses bindings using `-each-bindings-adapter`
   - Creates a Flecs system entity with `ecs_system_init`
   - Registers a callback that iterates over matches
3. Uses JVM's foreign function interface for callbacks

### jank Limitations

jank cannot directly use C++ callbacks/lambdas from jank functions because:
1. C++ templates can't be JIT-resolved (they generate symbols the JIT can't find)
2. jank functions can't be passed as C++ function pointers

### Solution: Query-Based Iteration

Instead of registering callbacks with Flecs, the jank implementation:
1. Uses Flecs C API directly via header requires
2. Creates queries with `ecs_query_init`
3. Provides iteration helpers (`query-iter`, `iter-next`, `iter-count`)
4. Uses `with-each` macro for clean iteration syntax

## What I Did

### Implemented `vybe.flecs` namespace (`src/vybe/flecs.jank`)

Key functions:

```clojure
;; World management
(make-world)       ; Create full Flecs world
(make-world-mini)  ; Create minimal world
(destroy-world! w) ; Destroy world

;; Entity management
(eid world e)      ; Get entity ID from keyword/string
(new-entity world) ; Create new entity

;; System/Pipeline
(system-run world system-id delta-time) ; Run a specific system
(progress world delta-time)              ; Run all pipeline systems

;; Query iteration (replaces with-system callback)
(query-str world "Position, Velocity") ; Create query from DSL string
(query-iter world query)               ; Get iterator
(iter-next iter)                       ; Advance iterator
(iter-count iter)                      ; Entity count in current table
(iter-entity-at iter idx)              ; Get entity at index

;; Macro for clean iteration
(with-each world query [e]
  (println "Entity:" e))
```

### API Design Differences from Clojure

| Clojure vybe.flecs | jank vybe.flecs |
|-------------------|-----------------|
| `with-system` registers callback | `with-each` iterates manually |
| Automatic component binding | Manual field access via `field-ptr` |
| Uses JVM Panama for FFI | Uses header require + cpp/ |

## Commands I Ran

```bash
# Read the Clojure implementation
grep -A100 "defmacro with-system" /Users/pfeodrippe/dev/vybe/src/vybe/flecs.clj

# Read the -system function
Read /Users/pfeodrippe/dev/vybe/src/vybe/flecs.clj lines 2159-2228
```

## What's Next

1. **Test the implementation** - Create a test file that uses `vybe.flecs`
2. **Add component helpers** - Functions to set/get component data on entities
3. **Improve with-each** - Support automatic component field binding based on query
4. **Add observer support** - Implement `with-observer` for event-driven systems

## File Structure

```
src/vybe/
├── flecs.jank   ; Flecs C API wrappers + with-each macro
└── util.jank    ; General utilities (->cpp-type, v->p, etc.)
```

## Usage Example

```clojure
(ns my-flecs-demo
  (:require
   [vybe.flecs :as vf]))

;; Create world
(def world (vf/make-world))

;; Create entities
(def e1 (vf/new-entity world))
(def e2 (vf/new-entity world))

;; Create a query
(def position-query (vf/query-str world "Position"))

;; Iterate with the query
(vf/with-each world position-query [entity-id]
  (println "Entity:" entity-id))

;; Cleanup
(vf/destroy-world! world)
```
