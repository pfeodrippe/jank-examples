# Why `or` Doesn't Work with C++ Native Bools in jank

**Date**: 2025-12-05

## The Problem

When using `or` with C++ native boolean values accessed via `cpp/.-field`, the code fails to compile:

```clojure
;; This FAILS:
(defn imgui-wants-input? []
  (let [io (imgui/GetIO)]
    (or (cpp/.-WantCaptureMouse io)
        (cpp/.-WantCaptureKeyboard io))))
```

C++ error:
```
error: non-const lvalue reference to type 'bool' cannot bind to an initializer list temporary
```

## Root Cause

The `or` macro expands to code that returns **the truthy value itself**, not `true`:

```clojure
;; Running: (macroexpand-1 '(or a b))
;; Returns:
(let [G__3 a]
  (if G__3
    G__3      ;; <-- Returns the VALUE, not true!
    (or b)))
```

In jank's C++ codegen for native types, this creates something like:

```cpp
bool & result{ };  // ERROR: Can't default-initialize a reference!
if (condition) { result = value1; } else { result = value2; }
```

C++ references **must** be initialized at declaration - you cannot have `bool & var{ }`.

## The Solution

Use `if/else` with explicit return values instead of `or`:

```clojure
;; This WORKS:
(defn imgui-wants-input? []
  (let [io (imgui/GetIO)]
    (if (cpp/.-WantCaptureMouse io)
      true  ;; Explicit true, not the native value
      (cpp/.-WantCaptureKeyboard io))))
```

With `if/else` returning explicit values (`true` or the second bool), jank can generate proper C++ without needing a reference variable to hold the result.

## Alternative Workaround

If you need to use `or`, box the values first:

```clojure
;; Also WORKS (but less efficient):
(defn imgui-wants-input? []
  (let [io (imgui/GetIO)]
    (or (boolean (cpp/.-WantCaptureMouse io))
        (boolean (cpp/.-WantCaptureKeyboard io)))))
```

This converts the native C++ bools to jank booleans, which can be handled by the `or` macro normally.

## Summary

| Pattern | Works? | Why |
|---------|--------|-----|
| `(or (cpp/.-field1 obj) (cpp/.-field2 obj))` | NO | Returns native value, creates `bool &` |
| `(if (cpp/.-field1 obj) true (cpp/.-field2 obj))` | YES | Returns explicit values |
| `(or (boolean ...) (boolean ...))` | YES | Converts to jank booleans first |

## Is This a Bug?

This is arguably a limitation in jank's C++ codegen for the `or` macro when dealing with native types. The macro itself is correct for jank semantics (returning the truthy value), but the codegen creates invalid C++ for native reference types.

A potential fix in the jank compiler would be to detect when `or` operates on native types and generate different C++ code that doesn't use references.

## Key Lesson

When working with C++ native types in jank, prefer explicit `if/else` over `or`/`and` macros to avoid reference initialization issues.
