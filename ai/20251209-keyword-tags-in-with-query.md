# Keywords as Tags in with-query

## Date: 2025-12-09

## Summary

Added support for using keywords as tags in `with-query`, making it consistent with how tags are added via `assoc` on the world.

## Changes Made

### 1. Added `mangle-keyword` function in `src/vybe/type.jank`

```clojure
(defn mangle-keyword
  "Convert a keyword to a Flecs-safe identifier.
   For qualified keywords: :foo.bar/baz -> foo_bar__baz
   For simple keywords: :my-tag -> my_tag"
  [kw]
  ...)
```

### 2. Updated `build-query-string` in `src/vybe/type.jank`

Now handles keywords by mangling them:
- `:walking` -> `"walking"`
- `:my-tag` -> `"my_tag"`
- `:foo/bar` -> `"foo__bar"`

### 3. Updated `-add-item-to-entity!` in `src/vybe/flecs.jank`

Now uses `mangle-keyword` when adding keyword tags, ensuring consistency between tag storage and query lookups.

### 4. Updated `with-query` macro in `src/vybe/flecs.jank`

Now recognizes keywords as valid query terms (tags) separate from components.

### 5. Fixed test `entity-update-test` in `test/vybe/flecs_test.jank`

Updated to properly test:
- Query with non-existent tag returns 0 results
- Query with `:walking` tag returns only entities with that tag
- Query with only Position component returns all entities with Position

## Usage Example

```clojure
;; Add entities with tags
(assoc w :bob [:walking (Position {:x 10.0 :y 20.0})])
(assoc w :alice [(Position {:x 5.0 :y 15.0})])

;; Query with keyword tag - only returns bob
(vf/with-query w [p Position
                  _ :walking]
  (println (:x p) (:y p)))

;; Query without tag - returns both
(vf/with-query w [p Position]
  (println (:x p) (:y p)))
```

## Keyword Mangling Rules

| Input | Output |
|-------|--------|
| `:walking` | `"walking"` |
| `:my-tag` | `"my_tag"` |
| `:foo/bar` | `"foo__bar"` |
| `:foo.bar/baz-qux` | `"foo_bar__baz_qux"` |

## Learned

- Keywords in jank work with `name`, `namespace`, and `keyword?` as expected
- The `mangle-char` function replaces `.` and `-` with `_`
- Qualified keywords use `__` to separate namespace from name
- Jank's namespace reload behavior can cause component registrations to be lost - defining fresh components in REPL sessions is more reliable for testing

## Additional Fix: defonce for atoms

Changed `def` to `defonce` for all atom definitions in `vybe.type` to preserve state across reloads:
- `comp-meta-cache`
- `comp-registry`
- `comp-id-cache`

## Additional Fix: eid auto-registration

Modified `eid` function to auto-register entities (keywords/strings) if they don't exist in Flecs. This means querying with `:inexistent` tag no longer fails - the tag entity is created automatically.

## Additional Fix: cpp/raw consolidation

Moved all C++ helper functions into a single `cpp/raw` block at the top of the file (after ns declaration). This is required because:
1. C++ functions must be defined before they're used
2. Having one cpp/raw block per namespace keeps code organized

## Test Results

All 35 tests pass:
- vybe.flecs-test: 16 tests, 34 assertions
- vybe.type-test: 19 tests, 83 assertions
