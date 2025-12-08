# Plan: Fix with-query Macro

**Date**: 2025-12-08

## Problems Encountered

1. **Gensym issues**: `i#` gets different names in nested syntax-quotes
2. **recur-through-try**: jank's `while` uses `try` internally, `dotimes` uses `recur`
3. **Capture issues**: `ecs_iter_t` can't be captured by closures for `doseq`
4. **cpp/raw symbol names**: jank symbols with dashes become subtraction in C++

## Simpler Solution

For now, just return entities as a vector using a C++ helper function. The `with-query` macro is complex due to jank's limitations. Let's simplify:

1. Create `query-entities` function that returns all entities matching a query as a vector
2. Users can then iterate with regular Clojure `doseq`

```clojure
;; Usage:
(doseq [e (vf/query-entities w "TestTag")]
  (println "Entity:" e))
```

## Code

Use cpp/raw to define a helper function that does the iteration in pure C++.
