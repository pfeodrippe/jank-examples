# with-query Macro Implementation - Final Solution

**Date**: 2025-12-08

## Summary

Successfully implemented the `with-query` macro for iterating over Flecs query results in jank.

## Problems Encountered & Solutions Tried

Several approaches failed due to jank limitations:

1. **Gensym issues**: `i#` auto-gensym creates different symbols in nested syntax-quotes
2. **recur-through-try**: jank's `while` and `dotimes` use `try` internally, blocking `recur`
3. **Closure capture**: `ecs_iter_t` (native struct) cannot be captured by jank closures
4. **cpp/raw symbol names**: Symbols with dashes become subtraction in C++ (e.g., `my-var` → `my - var`)

## Final Solution: C++ Helper Function

Instead of fighting jank's macro limitations, implemented iteration in pure C++:

```cpp
jank::runtime::object_ref vybe_query_entities(ecs_world_t* w, ecs_query_t* q) {
  jank::runtime::object_ref result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  ecs_iter_t it = ecs_query_iter(w, q);
  while (ecs_query_next(&it)) {
    for (int i = 0; i < it.count; i++) {
      ecs_entity_t entity = it.entities[i];
      result = jank::runtime::conj(result, jank::runtime::make_box(static_cast<int64_t>(entity)));
    }
  }
  return result;
}
```

Key insight: Use `int64_t` not `native_integer` - the latter doesn't exist in jank's namespace.

## API Usage

```clojure
;; Simple iteration with query-entities
(doseq [e (vf/query-entities w "TestTag")]
  (println "Entity:" e))

;; with-query macro (more ergonomic)
(vf/with-query w [:vf/expr "TestTag", e :vf/entity]
  (println "Entity:" e))
```

The `with-query` macro:
- Takes bindings with `:vf/expr` for the query string
- Takes `:vf/entity` to bind each entity
- Returns a vector of results (uses `mapv` internally)

## Commands Run

```bash
# Run tests
./run_tests.sh
```

Output:
```
Running vybe.flecs tests...
  world-creation-test passed
  world-mini-creation-test passed
  world-ptr-test passed
  new-entity-test passed
  eid-test passed
  progress-test passed
  defn*-generates-macro-test passed
  query-str-test passed
  with-query-test passed
All tests passed!
```

## Files Modified

- `/Users/pfeodrippe/dev/something/src/vybe/flecs.jank` - Added C++ helper and macros
- `/Users/pfeodrippe/dev/something/test/vybe/flecs_test.jank` - Added with-query test (in test dir, not src!)
- `/Users/pfeodrippe/dev/something/run_tests.sh` - Updated module path to `src:test` (colon-separated)

## Key Learnings

1. **When jank macro limitations hit, use cpp/raw** - Native C++ iteration avoids closure capture issues
2. **Use int64_t for jank integers** - Not `native_integer` which doesn't exist
3. **jank::runtime::conj works** - Can build vectors from C++ using `conj`
4. **jank::runtime::make_box** - Creates boxed values from native types

## Next Steps

- Consider adding component field access to the `with-query` macro
- May need to expose field pointers through C++ helpers for component data access
