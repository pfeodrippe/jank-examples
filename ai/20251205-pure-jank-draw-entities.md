# Pure jank draw-entities! Function

**Date**: 2025-12-05

## What Was Learned

### Converting draw_entities from C++ to pure jank

Successfully converted the C++ `draw_entities` function to pure jank using:
1. C++ accessor helpers for entity data by index
2. Global state pattern for getting Jolt body positions
3. `dotimes` for iteration in jank
4. Raylib header requires (`rl/DrawCircle`, `rl/Color.`, etc.)

### Key patterns used:

#### 1. Entity accessor helpers (C++)
Added small C++ helper functions to access entity data by index:
```cpp
inline int64_t entity_jolt_id(int64_t idx) {
    if (idx < 0 || idx >= (int64_t)get_entities().size()) return 0;
    return get_entities()[idx].jolt_id;
}
inline double entity_radius(int64_t idx) { ... }
inline int64_t entity_r(int64_t idx) { ... }
inline int64_t entity_g(int64_t idx) { ... }
inline int64_t entity_b(int64_t idx) { ... }
```

#### 2. Position accessor with global state pattern
Since jolt_body_get_position uses out parameters, used a global struct:
```cpp
struct EntityPos { float x, y, z; };
static EntityPos* g_entity_pos_ptr = nullptr;
inline EntityPos& get_entity_pos_result() {
    if (!g_entity_pos_ptr) g_entity_pos_ptr = new EntityPos{0,0,0};
    return *g_entity_pos_ptr;
}
inline void entity_get_position(jank::runtime::object_ref world_box, int64_t jolt_id) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(world_box);
    EntityPos& pos = get_entity_pos_result();
    jolt_body_get_position(o->data.data, (uint32_t)jolt_id, &pos.x, &pos.y, &pos.z);
}
inline float entity_pos_x() { return get_entity_pos_result().x; }
inline float entity_pos_y() { return get_entity_pos_result().y; }
inline float entity_pos_z() { return get_entity_pos_result().z; }
```

#### 3. Text formatting helper
jank can't easily do sprintf formatting, so used a C++ helper with ASCII value for percent:
```cpp
inline void draw_height_label(int64_t x, int64_t y, int64_t height) {
    char buf[16];
    char fmt[] = { 37, 'd', 0 };  // 37 is ASCII for percent
    snprintf(buf, sizeof(buf), fmt, (int)height);
    DrawText(buf, (int)x - 10, (int)y - 5, 10, BLACK);
}
```

### Important jank patterns discovered:

1. **Use `let` for destructuring**: Use `let` (not `let*`) to enable destructuring like `[a b] (some-fn)`. `let*` does NOT support destructuring.

2. **Percent sign in cpp/raw**: The `%` character causes jank parser issues in cpp/raw blocks - use ASCII value 37 instead

3. **jank::runtime::object_ref**: When passing jank objects to C++, use `jank::runtime::object_ref` parameter type and extract with `expect_object<obj::opaque_box>`

4. **Native value types can't be boxed/returned**: Native C++ value types like `Color` can't be boxed with `cpp/box` (only pointers can). Inline native type construction at point of use instead of returning from helper functions.

## Changes Made

### 1. Added C++ entity accessor helpers
**File**: `src/my_integrated_demo.jank` (lines 211-256)
- `entity_jolt_id(idx)` - get Jolt body ID
- `entity_radius(idx)` - get entity radius
- `entity_r/g/b(idx)` - get entity color components
- `entity_get_position(world_box, jolt_id)` - populate global position struct
- `entity_pos_x/y/z()` - read position components
- `draw_height_label(x, y, height)` - draw formatted height text

### 2. Added pure jank drawing functions with `let` destructuring
**File**: `src/my_integrated_demo.jank`

Helper functions that return vectors for destructuring:
```clojure
(defn get-entity-position
  "Get entity position from Jolt. Returns [x y z]."
  [jolt-world jolt-id]
  (cpp/entity_get_position jolt-world jolt-id)
  [(cpp/entity_pos_x) (cpp/entity_pos_y) (cpp/entity_pos_z)])

(defn get-entity-color
  "Get entity color. Returns [r g b]."
  [idx]
  [(cpp/entity_r idx) (cpp/entity_g idx) (cpp/entity_b idx)])
```

Main draw function using `let` with destructuring:
```clojure
(defn draw-entity!
  "Draw a single entity at index idx. Pure jank using raylib header requires."
  [jolt-world idx]
  (let [jolt-id (cpp/entity_jolt_id idx)
        radius (cpp/entity_radius idx)
        [er eg eb] (get-entity-color idx)                    ;; destructuring!
        [px py pz] (get-entity-position jolt-world jolt-id)  ;; destructuring!
        [screen-x screen-y] (physics-to-screen px pz)        ;; destructuring!
        ;; ... calculate brightness, colors, positions ...
        ]
    ;; Draw shadow, ball, outline, height label
    (rl/DrawCircle shadow-x shadow-y (cpp/float. (* sr 0.8)) (rl/Fade (rl/BLACK) (cpp/float. 0.2)))
    ;; Inline Color construction (can't box native value types)
    (rl/DrawCircle ball-x ball-y (cpp/float. sr)
                   (rl/Color. (cpp/uint8_t. col-r) (cpp/uint8_t. col-g) (cpp/uint8_t. col-b) (cpp/uint8_t. 255)))
    (rl/DrawCircleLines ball-x ball-y (cpp/float. sr) (rl/BLACK))
    (cpp/draw_height_label ball-x ball-y (int py))
    nil))

(defn draw-entities!
  "Draw all entities. Pure jank - iterates over entity storage."
  [world]
  (let [count (entity-count)]  ;; use let, not let*
    (dotimes [i count]
      (draw-entity! world i))
    nil))
```

### 3. Removed old C++ draw_entities function
Replaced with comment noting it's now in pure jank.

## Commands Used

```bash
# Run demo to test changes
./run_integrated.sh
```

## Summary of Pure jank Draw Functions

| Function | Purpose | C++ Equivalent |
|----------|---------|----------------|
| `draw-entities!` | Draw all entities with physics positions | `draw_entities()` |
| `draw-entity!` | Draw single entity at index | (part of for loop) |

## What's Next

- Continue looking for more C++ functions in cpp/raw that can be converted to pure jank
- Consider converting `draw_grid` if performance is acceptable (currently uses C++ loop)
- Look at other rendering helpers that might be convertible
