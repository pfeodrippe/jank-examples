# Learning: cpp/new for REPL-Persistent Mutable C++ Primitives

**Date**: 2024-12-03

## What I Learned

### 1. The Problem

When using `cpp/bool.` with `cpp/box` to store mutable C++ primitives for ImGui widgets:

```clojure
(defonce is-paused (cpp/box (cpp/& (cpp/bool. false))))
```

The checkbox works initially, but **the value is lost when any REPL eval happens**.

### 2. Root Cause: Stack vs Heap Allocation

- `cpp/bool.` creates a **stack-allocated** temporary
- When the REPL eval scope ends, the stack is freed
- The pointer inside `cpp/box` becomes **dangling**
- Next access reads garbage or crashes

### 3. The Solution: Use `cpp/new`

```clojure
;; cpp/new allocates on GC heap - persists across REPL evals!
(defonce is-paused (cpp/box (cpp/new cpp/bool false)))

;; Usage with ImGui Checkbox (needs raw pointer)
(imgui/Checkbox "Paused" (cpp/unbox cpp/bool* is-paused))

;; Reading the value (unbox + dereference)
(defn paused? [] (cpp/* (cpp/unbox cpp/bool* is-paused)))
```

### 4. How jank Memory Allocation Works

| Construct | Allocation | Persistence |
|-----------|------------|-------------|
| `cpp/bool.` | Stack | Temporary - freed at scope end |
| `cpp/new cpp/bool` | GC heap (Boehm GC) | Persists until unreachable |
| `cpp/box` | Wraps pointer in jank object | Required to store in vars |

### 5. Pattern for ImGui Mutable Primitives

```clojure
;; Define (once, at load time)
(defonce my-float (cpp/box (cpp/new cpp/float 1.0)))

;; Pass to ImGui widget
(imgui/SliderFloat "Value" (cpp/unbox cpp/float* my-float) 0.0 10.0)

;; Read current value
(cpp/* (cpp/unbox cpp/float* my-float))
```

## Files Modified

1. **`/Users/pfeodrippe/dev/something/src/my_integrated_demo.jank`**
   - Line 422: Changed `is-paused` to use `cpp/new` for GC heap allocation
   - Line 438: Uses `cpp/unbox cpp/bool* is-paused` for ImGui Checkbox
   - Line 503: Updated `paused?` to properly unbox before dereferencing

## Key Insight

The `cpp/new` operator in jank allocates via `GC_malloc` (Boehm garbage collector), which:
- Survives REPL evaluations
- Is automatically freed when unreachable
- Can be wrapped in `cpp/box` for storage in jank vars

This makes `cpp/new` the correct choice for any mutable C++ primitive that needs to persist across REPL interactions.

## What's Next

1. Test the fix by running `./run_integrated.sh` and verifying Paused checkbox persists across REPL evals
2. Consider converting other mutable primitives (time-scale, spawn-count) from cpp/raw to pure jank using this pattern
3. Document this pattern in the native resources guide
