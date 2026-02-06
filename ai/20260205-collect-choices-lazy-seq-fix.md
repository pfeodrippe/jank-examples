# 2026-02-05: Fix collect-choices / vec Crash

## What was done

### Root Cause
The crash `invalid sequence: [...]` was NOT caused by lazy seqs or `collect-choices` being a top-level `defn-` vs local `fn`. The actual issue was that calling `vec` through an intermediate `defn-` function (`set-choice-entities!`) triggered a jank bug. Specifically:

- `update-available-choices!` called `set-choice-entities!(vec (collect-choices ...))` 
- `set-choice-entities!` then called `(reset! *choices-cache (vec choices))` -- double `vec`
- Even after removing the inner `vec`, calling `reset!` via a separate `defn-` still crashed

The working committed code does `(reset! *choices (vec (collect-choices current)))` directly inline -- no intermediate function call.

### Fix
Inlined the `reset!` call directly in `update-available-choices!`:
```clojure
(reset! *choices-cache (vec (collect-choices current)))
```
Instead of going through `set-choice-entities!`.

### What was NOT changed
- ECS infrastructure (`vt/defcomp EntryOrder`, `EntryType`, `FileWatchState`) still defined
- `get-world`, `reset-world!` still present
- `history-count`, `choice-count` ECS query helpers still present
- `set-choice-entities!`, `clear-choice-entities!` still defined (just not called yet)
- `collect-choices` kept as local `fn` inside `update-available-choices!` (matching committed working pattern)

### Debug printlns removed from `load-story!`

### Tested
- `make fiction` runs successfully
- Story loads, choices work, multiple selections tested, clean exit

## jank Bug Learned
- Passing the result of `vec` through an intermediate `defn-` function and then calling `reset!` can trigger `invalid sequence` errors
- Inlining the `reset!` call avoids this
- This is likely related to how jank handles persistent_vector across JIT-compiled function boundaries

## Files modified
- `src/fiction/state.jank` - inlined reset in update-available-choices!, removed debug prints

## What to do next
1. Incrementally add ECS entity operations (carefully, testing after each change)
2. Add `vf/defsystem` for file watching
3. The ECS entity operations need to avoid the intermediate `defn-` pattern -- either inline everything or find the exact jank bug trigger
