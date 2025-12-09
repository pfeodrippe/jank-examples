# Fix Plan: Flaky Test in children-test

## Date: 2025-12-09

## Root Cause Analysis

After deep analysis, the flaky test issue is caused by **function identity instability during JIT recompilation**.

### The Problem Chain

1. `get-in w [:sun Position :x]` calls entity's `:get` handler with `Position` (a function)
2. Entity's `:get` handler (line 717 in flecs.jank) calls `vt/resolve-descriptor k`
3. `resolve-descriptor` (line 498-509 in type.jank) looks up `k` in `comp-registry`:
   ```clojure
   (or (get @comp-registry comp)  ; <-- Function identity lookup
       (when-let [comp-name (:vt/comp-name (meta comp))]
         (resolve-descriptor-by-name comp-name)))
   ```

4. **The Issue**: When jank's JIT recompiles functions, the `Position` function object in the test may be a **different object** than the one stored as a key in `comp-registry`. This causes `(get @comp-registry comp)` to return `nil`.

5. The fallback `(:vt/comp-name (meta comp))` should work, but metadata access on JIT-recompiled functions may also be unstable.

### Why println "Fixes" It (Heisenbug)

`println` forces synchronization/evaluation that:
- Ensures all deferred computations complete
- May trigger registry updates before the test assertion
- Changes timing so the "right" function object is used

## Solution: Use Name-Based Lookup by Default

Instead of relying on function identity (which is unstable during JIT), always use the component name for lookup.

### Changes Required

#### 1. Fix `resolve-descriptor` in `src/vybe/type.jank`

Current (lines 498-509):
```clojure
(defn resolve-descriptor
  [comp]
  (cond
    (map? comp) comp
    :else
    (or (get @comp-registry comp)  ; <-- Unstable!
        (when-let [comp-name (:vt/comp-name (meta comp))]
          (resolve-descriptor-by-name comp-name)))))
```

Fixed:
```clojure
(defn resolve-descriptor
  [comp]
  (cond
    (map? comp) comp
    :else
    ;; Always try name-based lookup first (more stable)
    ;; Function identity can change due to JIT recompilation
    (let [comp-name (or (:vt/comp-name (meta comp))
                        ;; Check if comp itself is a string name
                        (when (string? comp) comp))]
      (if comp-name
        ;; Name-based lookup is stable across JIT recompilation
        (resolve-descriptor-by-name comp-name)
        ;; Fallback to function identity (for cases without metadata)
        (get @comp-registry comp)))))
```

#### 2. Ensure `make-comp-constructor` sets metadata correctly

The constructor function created by `make-comp-constructor` (line 1288-1309) already has metadata:
```clojure
(with-meta (fn [values]
             (create-user-type-instance desc values))
           {:vt/comp-name comp-name})
```

This should work, but we need to ensure metadata survives JIT recompilation.

#### 3. Alternative: Force eager evaluation of component name

If metadata is unstable, we can store the name in a def binding:

In `defcomp` macro (line 1311-1344), add name registration:
```clojure
(defmacro defcomp
  [sym fields]
  (let [comp-name (mangle-name *ns* sym)]
    `(do
       ;; Register name -> descriptor mapping eagerly
       (register-descriptor-by-name (make-comp ~comp-name ~fields))
       ;; Create constructor
       (def ~sym
         (make-comp-constructor (resolve-descriptor-by-name ~comp-name))))))
```

Wait, that won't work because `make-comp` returns a fresh descriptor.

Better approach: **Store constructor-to-name mapping separately** so we can always look up by name.

## Final Fix Strategy

The simplest, most robust fix:

1. **In entity's `:get` handler**, when we have a component constructor function, extract its name via metadata and use name-based lookup:

In `flecs.jank` line 717-721:
```clojure
;; Component constructor function
:else
(let [comp-desc (vt/resolve-descriptor k)]
  ...)
```

Change to:
```clojure
;; Component constructor function
:else
(when-let [comp-name (or (:vt/comp-name (meta k))
                          (:name (vt/resolve-descriptor k)))]
  (let [comp-desc (vt/resolve-descriptor-by-name comp-name)]
    ...))
```

This ensures we always use the stable name-based lookup.

2. **Update `resolve-descriptor` to prioritize name-based lookup**:

```clojure
(defn resolve-descriptor
  [comp]
  (cond
    (map? comp) comp
    :else
    ;; Try name-based lookup first (stable across JIT)
    (or (when-let [comp-name (:vt/comp-name (meta comp))]
          (resolve-descriptor-by-name comp-name))
        ;; Fallback to function identity
        (get @comp-registry comp))))
```

## Implementation Steps

1. Modify `resolve-descriptor` in `src/vybe/type.jank` to prioritize name-based lookup
2. Test with multiple runs to verify fix:
   ```bash
   clj-nrepl-eval -p 5557 "
   (doseq [i (range 1 21)]
     (let [results (clojure.test/run-tests 'vybe.flecs-test)]
       (when (> (:fail results) 0)
         (println \"FAILED at run\" i))))
   " 2>&1 | grep -E '(FAILED|run)'
   ```
3. If still flaky, add name extraction in entity's `:get` handler

## Expected Outcome

After the fix:
- `resolve-descriptor Position` will always use `(:vt/comp-name (meta Position))` which is `"vybe_flecs_test__Position"`
- This name is stable across JIT recompilation
- `resolve-descriptor-by-name` looks up in `-comp-name-registry` which was populated at `defcomp` time
- Test should pass consistently 20/20 times
