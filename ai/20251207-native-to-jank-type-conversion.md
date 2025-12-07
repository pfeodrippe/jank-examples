# Native to jank Type Conversion

**Date**: 2025-12-07

## Problem

When converting C++ functions to jank, mixing native C++ types (like `float`, `unsigned char`) with jank arithmetic caused type errors like:
- "invalid object type (expected integer found real)"
- "TODO: port long" (long not implemented yet)

## Root Cause

Native C++ values and jank boxed values don't mix well in arithmetic. When you have:
- Native floats from `cpp/` functions
- Jank reals from `double` or arithmetic
- Mixed operations like `(* native-float jank-number)`

The types can become inconsistent and cause runtime errors.

## Solution: Consistent Type Domains

Keep types consistent within each domain:

### 1. For values used in jank arithmetic

Convert native values to jank at the source:
```clojure
;; Native float -> jank real
(double (cpp/* float_ptr))

;; Native int/unsigned char -> jank integer
(int (cpp/.-field entity))

;; Native from C++ getter -> jank real
(double (cpp/get_view_scale))
```

### 2. For functions that return values for jank arithmetic

```clojure
(defn physics-to-screen
  [px pz]
  ;; Convert native floats to jank numbers first
  (let [offset-x (double (cpp/get_view_offset_x))
        offset-y (double (cpp/get_view_offset_y))
        scale (double (cpp/get_view_scale))]
    ;; Now arithmetic is all jank-to-jank
    [(+ offset-x (* px scale))
     (- offset-y (* pz scale))]))
```

### 3. For out-parameters with atoms (avoid boxing overhead)

```clojure
;; Store position in atoms (jank reals)
(def ^:private *pos-x (atom 0.0))
(def ^:private *pos-y (atom 0.0))
(def ^:private *pos-z (atom 0.0))

(defn get-entity-position!
  [jolt-world jolt-id]
  (let [x_ptr (cpp/new cpp/float 0.0)
        y_ptr (cpp/new cpp/float 0.0)
        z_ptr (cpp/new cpp/float 0.0)]
    (jolt/jolt_body_get_position ...)
    ;; Convert to jank reals when storing
    (reset! *pos-x (double (cpp/* x_ptr)))
    (reset! *pos-y (double (cpp/* y_ptr)))
    (reset! *pos-z (double (cpp/* z_ptr)))
    nil))

(defn entity-pos-x [] @*pos-x)
```

### 4. For final Raylib calls (convert back to native)

```clojure
;; jank integer -> native int for C++ calls
(rl/DrawCircle (int screen-x) (int screen-y)
               (cpp/float. sr) rl/BLACK)

;; jank integer -> native for TextFormat
(rl/DrawText (rl/TextFormat #cpp "%d" (cpp/int. (int py))) ...)
```

## Key Lessons

1. **`long` is not yet implemented** - use `int` instead
2. **`double` converts native floats to jank reals**
3. **`int` converts native integers to jank integers**
4. **Keep arithmetic in one type domain** - all jank or all native
5. **Convert at boundaries** - when crossing between domains

## What NOT to Use

- `cpp/unbox` - for opaque pointer boxing only, not type conversion
- `long` - not yet implemented in jank
- Mixed native/jank arithmetic - causes type errors

## Files Changed

- `src/my_integrated_demo.jank`:
  - `get-entity-position!` - uses atoms to avoid boxing in hot path
  - `entity-pos-x/y/z` - accessor functions return jank reals
  - `get-entity-color` - converts native unsigned chars to jank integers
  - `physics-to-screen` - converts native floats to jank before arithmetic
  - `draw-entity!` - uses consistent jank types for all calculations
