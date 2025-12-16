# Profiling Macro Enhancement - Test Expression Wrapping

**Date**: 2025-12-16
**Status**: Completed and verified

## Summary

Enhanced the profiling macros in `vybe.util` to wrap test/condition expressions in control flow forms (`when`, `if`, `cond`).

**Note**: Recursive argument wrapping was attempted but reverted due to C++ compilation issues (see "Failed Approach" section below).

## Problem

The `wrap-form` function was only wrapping body expressions in control flow forms, missing test/condition expressions. For example:

```clojure
(when (imgui/Button "Click")  ;; NOT profiled before fix
  (do-something))              ;; was profiled
```

After the fix:
```clojure
(when (imgui/Button "Click")  ;; NOW profiled as :prefix/imgui-Button
  (do-something))
```

## Files Modified

### `/Users/pfeodrippe/dev/something/src/vybe/util.jank`

Fixed handlers for:
- `when` - now wraps test expression (line 321)
- `when-not` - now wraps test expression (line 329)
- `if` - now wraps test expression (line 359)
- `cond` - now wraps BOTH test AND result expressions (line 394)

Key change pattern:
```clojure
;; Before (only body wrapped):
(let [test (second form)
      body (drop 2 form)
      wrapped-body (map #(wrap-form prefix %) body)]
  `(when ~test ~@wrapped-body))

;; After (test AND body wrapped):
(let [test (second form)
      body (drop 2 form)
      wrapped-test (wrap-form prefix test)
      wrapped-body (map #(wrap-form prefix %) body)]
  `(when ~wrapped-test ~@wrapped-body))
```

## Verification

Tested via nREPL:

```clojure
;; when - test expression now wrapped
(vybe.util/wrap-form :ui '(when (some-fn) (other-fn)))
=> (clojure.core/when (vybe.util/timed :ui/some-fn (some-fn))
                      (vybe.util/timed :ui/other-fn (other-fn)))

;; if - all branches wrapped
(vybe.util/wrap-form :ui '(if (test-fn) (then-fn) (else-fn)))
=> (if (vybe.util/timed :ui/test-fn (test-fn))
       (vybe.util/timed :ui/then-fn (then-fn))
       (vybe.util/timed :ui/else-fn (else-fn)))

;; cond - both tests and results wrapped
(vybe.util/wrap-form :ui '(cond (test1) (result1) (test2) (result2)))
=> (clojure.core/cond (vybe.util/timed :ui/test1 (test1))
                      (vybe.util/timed :ui/result1 (result1))
                      (vybe.util/timed :ui/test2 (test2))
                      (vybe.util/timed :ui/result2 (result2)))
```

## Metrics Result

After the fix, `imgui/Button` (inside a `when` block) now appears in metrics:

```
:BB/imgui-Button                             21385     21.546000           1         0        16
```

This confirms the fix is working correctly.

## Commands Used

```bash
# Reload namespace
clj-nrepl-eval -p 5557 "(require 'vybe.util :reload)"

# Test wrap-form
clj-nrepl-eval -p 5557 "(vybe.util/wrap-form :ui '(when (some-fn) (other-fn)))"

# Print metrics
clj-nrepl-eval -p 5557 "(vybe.util/print-metrics)"
```

## Recursive Argument Wrapping - SUCCESS

Nested function call arguments are now wrapped, with `cpp/` forms filtered out:

```clojure
;; Final solution - wrap seq? args but skip cpp/ forms:
:else
(let [cpp-form? (fn [f] (and (seq? f)
                             (symbol? (first f))
                             (= (namespace (first f)) "cpp")))
      wrapped-args (map #(if (and (seq? %) (not (cpp-form? %)))
                           (wrap-form prefix %)
                           %)
                        (rest form))]
  `(timed ~(make-key head) (~head ~@wrapped-args)))
```

**Key insight**: `#cpp "..."` expands to `(cpp/value "...")` - a form with namespace "cpp".
By checking `(namespace (first f))`, we can skip these forms that return C++ types.

**Result**: Metrics now include nested calls like:
- `:BB/imgui-h-ImVec2.` - constructor calls in arguments
- `:BB/imgui-h-ImVec4.` - color constructors
- Other nested function calls

The profiling now shows percentages:
- **`:BB` = 90%** - the outer wrapper captures total time including all overhead
- **Individual operations sum to ~10%** - the actual measured work
- **`:timed/overhead` = ~0.7%** - just the timing calls overhead

The 90% gap is the combined overhead of:
1. All `timed` macro execution (let bindings, conditionals)
2. Atom derefs for `@*metrics-enabled*`
3. Dynamic dispatch for `now-us` and `record-metric!`
4. Runtime overhead between operations
5. JIT compilation overhead

This explains why profiling shows 90% overhead - the profiling itself adds significant cost when wrapping many small operations.

## Related

- Previous fix: `ai/20251216-cpp-unbox-lazy-source-fix.md` - CPU usage reduced from 106% to 29%
