# Pure jank draw-grid! and cpp/.size/.clear

**Date**: 2025-12-05

## What Was Learned

### 1. Converting draw_grid to pure jank

Successfully converted the C++ `draw_grid` function to pure jank using:
- `doseq` with `range` for iteration
- `rl/DrawLineV` with `rl/Vector2.` constructor
- `rl/Fade` and `rl/LIGHTGRAY` for grid color
- The existing jank `physics-to-screen` function

### 2. Native value types can't be captured in closures

**Problem**: When using `doseq` (which creates a closure), native C++ value types like `Color` cannot be captured.

**Error**:
```
Unable to capture 'grid-color', since its type 'Color' is not able to be
automatically converted to a jank object. You can mitigate this by
wrapping the value in a 'cpp/box' before capturing it.
```

**Solution**: Extract the drawing into a helper function that doesn't capture the Color:

```clojure
;; Helper function - no closure capture needed
(defn draw-grid-line!
  "Draw a single grid line. Helper to avoid capturing Color in closure."
  [x1 y1 x2 y2]
  (rl/DrawLineV (rl/Vector2. (cpp/float. x1) (cpp/float. y1))
                (rl/Vector2. (cpp/float. x2) (cpp/float. y2))
                (rl/Fade (rl/LIGHTGRAY) (cpp/float. 0.3))))

;; Main function uses helper
(defn draw-grid!
  "Draw grid lines. Pure jank using raylib header requires."
  []
  (doseq [i (range -50 51 5)]
    (let [[x1 y1] (physics-to-screen i -50.0)
          [x2 y2] (physics-to-screen i 50.0)]
      (draw-grid-line! x1 y1 x2 y2))
    (let [[x1 y1] (physics-to-screen -50.0 i)
          [x2 y2] (physics-to-screen 50.0 i)]
      (draw-grid-line! x1 y1 x2 y2)))
  nil)
```

### 3. Function definition order matters

Functions must be defined BEFORE they are called in jank. When adding `draw-grid!`, the `physics-to-screen` function had to be moved above it.

### 4. cpp/.size and cpp/.clear for std::vector

Discovered that `cpp/.size` and `cpp/.clear` work on std::vector, just like `cpp/.at`:

```clojure
;; Get vector size
(defn entity-count []
  (cpp/.size (cpp/get_entities)))

;; Clear vector
(defn clear-entities! []
  (cpp/.clear (cpp/get_entities)))
```

This eliminates the need for C++ wrapper functions!

## Changes Made

### 1. Converted draw_grid to pure jank
**File**: `src/my_integrated_demo.jank`

- Added `draw-grid-line!` helper function
- Replaced `(defn draw-grid! [] (cpp/draw_grid))` with pure jank implementation
- Moved `physics-to-screen` before `draw-grid!`

### 2. Removed C++ draw_grid and physics_to_screen
**File**: `src/my_integrated_demo.jank` (cpp/raw block)

Removed these C++ functions:
- `physics_to_screen(float px, float pz)`
- `draw_grid()`

### 3. Converted entity-count and clear-entities! to use cpp methods
**File**: `src/my_integrated_demo.jank`

```clojure
;; Before (using C++ wrappers)
(defn entity-count [] (cpp/flecs_entity_count))
(defn clear-entities! [] (cpp/flecs_clear_entities))

;; After (pure jank with cpp/.method)
(defn entity-count []
  (cpp/.size (cpp/get_entities)))
(defn clear-entities! []
  (cpp/.clear (cpp/get_entities)))
```

### 4. Removed C++ flecs_entity_count and flecs_clear_entities
**File**: `src/my_integrated_demo.jank` (cpp/raw block)

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## Summary of New cpp/ Methods

| cpp/ Method | C++ Equivalent | Use Case |
|-------------|----------------|----------|
| `cpp/.at` | `vec.at(idx)` | Element access (needs `cpp/size_t.` index) |
| `cpp/.size` | `vec.size()` | Get container size |
| `cpp/.clear` | `vec.clear()` | Clear container |

## Key Lessons

1. **Native types can't be captured in closures** - Use helper functions to avoid capturing `Color`, `Vector2`, etc.

2. **Function order matters** - Define dependencies before dependents

3. **cpp/.method works for std::vector** - Use `.at`, `.size`, `.clear` directly without C++ wrappers

4. **doseq with range for C++ for-loops** - `(doseq [i (range start end step)] ...)` replaces C++ for loops

## What's Next

- Look for other cpp/raw functions that could use cpp/.method
- Consider converting more rendering helpers if performance is acceptable
- Explore other std::vector methods (`.push_back`, `.empty`, etc.)
