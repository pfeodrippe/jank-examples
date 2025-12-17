# NS Re-evaluation Exception Fix

## Problem
Evaluating `(ns vybe.util (:require [clojure.string :as str]))` in the REPL threw an "unknown exception" when the namespace defined vars that shadow `clojure.core` functions (like `profile`).

## Root Cause
The issue was in jank's `ns::refer` function in `src/cpp/jank/runtime/ns.cpp`. When re-evaluating a `ns` form:

1. The `ns` macro calls `(refer 'clojure.core)` to refer all clojure.core vars
2. If the namespace has a local var (e.g., `vybe.util/profile`) that shadows a clojure.core var (e.g., `clojure.core/profile`)
3. The `refer` function tried to map `profile` to `clojure.core/profile`, but found it already mapped to `vybe.util/profile`
4. The condition `var->n != found_var->n && (found_var->n != clojure_core)` threw an error because:
   - `var->n` = clojure.core (the var we're trying to refer)
   - `found_var->n` = vybe.util (the existing local var)
   - Both conditions are true → error thrown

## Fix Applied
Modified `ns::refer` in `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/ns.cpp` to:

1. Also allow re-referring when the existing var is from the **current namespace** (local definition that shadows clojure.core)
2. Skip the refer (return early) to preserve local definitions instead of overwriting them

```cpp
// Added check for current namespace
if(var->n != found_var->n && found_var->n != clojure_core && found_var->n != this_ns)
{
  return err(...);
}
// Skip refer to preserve local definition
if(found_var->n == this_ns)
{
  return ok();
}
```

## Test Added
Created test: `/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/form/ns/pass-re-eval-with-shadowed-var.jank`

- **Without fix**: Fails with `time already refers to #'test.ns.shadow/time in ns test.ns.shadow`
- **With fix**: Passes with `:success`

## Workaround (Before Fix)
Add `:refer-clojure :exclude [shadowed-var]` to your ns form:

```clojure
(ns vybe.util
  (:refer-clojure :exclude [profile])
  (:require
   [clojure.string :as str]))
```

## Commands Used
```bash
# Verify test fails without fix
cd /Users/pfeodrippe/dev/jank/compiler+runtime
./build/jank run test/jank/form/ns/pass-re-eval-with-shadowed-var.jank  ;; FAILS

# Apply fix, rebuild
cmake --build build

# Verify test passes with fix
./build/jank run test/jank/form/ns/pass-re-eval-with-shadowed-var.jank  ;; :success
```
