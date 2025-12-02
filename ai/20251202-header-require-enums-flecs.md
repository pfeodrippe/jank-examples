# Learning: Header Require for Enums and Flecs Integration

**Date**: 2024-12-02

## What I Learned

### 1. Raylib Enum Constants are Bare Symbols

When using header requires, enum/constant values are NOT callable - use them as bare symbols:

```clojure
;; WRONG - "This value is not callable"
(rl/SetConfigFlags (bit-or (rl/FLAG_WINDOW_RESIZABLE) (rl/FLAG_MSAA_4X_HINT)))

;; CORRECT - bare symbols without parentheses
(rl/SetConfigFlags (bit-or rl/FLAG_WINDOW_RESIZABLE rl/FLAG_MSAA_4X_HINT))
(rl/IsKeyPressed rl/KEY_SPACE)
(rl/IsMouseButtonDown rl/MOUSE_BUTTON_LEFT)
```

### 2. Flecs Header Require Pattern

Flecs C API works via header require with empty scope:

```clojure
;; Require flecs
(ns my-demo
  (:require ["flecs.h" :as fl :scope ""]))

;; Create world - uses cpp/box to wrap C pointer
(defn flecs-create-world []
  (cpp/box (fl/ecs_mini)))
```

### 3. Opaque Box Extraction Requires cpp/raw Wrapper

Boxed pointers don't auto-unbox for header-require C functions. Minimal wrapper needed:

```clojure
;; This does NOT work - type mismatch errors
(fl/ecs_fini (cpp/unbox cpp/ecs_world_t* flecs-world))

;; Minimal cpp/raw wrapper needed for opaque_box extraction
(cpp/raw "
inline void flecs_fini_world(jank::runtime::object_ref flecs_world_box) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(flecs_world_box);
    ecs_fini(static_cast<ecs_world_t*>(o->data.data));
}
")

(defn flecs-destroy-world! [flecs-world]
  (cpp/flecs_fini_world flecs-world))
```

### 4. cpp/unbox Type String Sensitivity

Type strings must match exactly (including spaces):
- `cpp/ecs_world_t*` - type without space
- `"ecs_world_t *"` - boxed type may have space
- These don't match! Use cpp/raw wrapper instead.

## Rule Added to claude.md

**Rule 2 - Use jank/cpp prefix, cpp/raw is LAST RESORT**
- Always prefer header requires: `["raylib.h" :as rl :scope ""]`
- Use `cpp/` prefix for C++ interop: `cpp/box`, `cpp/unbox`, `cpp/.-field`, `cpp/.method`
- Use `let*` with `_` binding for void returns
- Only use `cpp/raw` when absolutely necessary (complex loops, callbacks, templates, ODR-safe globals)

## Files Modified

1. **`claude.md`** - Added Rule 2 about cpp/raw being last resort

2. **`src/my_integrated_demo.jank`**:
   - Added `["flecs.h" :as fl :scope ""]` require
   - Changed `flecs-create-world` to use `(cpp/box (fl/ecs_mini))`
   - Changed raylib enums to bare symbols: `rl/KEY_SPACE`, `rl/FLAG_WINDOW_RESIZABLE`
   - Added minimal `flecs_fini_world` wrapper for destroy

## Commands I Ran

```bash
# Test integrated demo
./run_integrated.sh 2>&1 &
sleep 8
```

## Key Insight

- **Header require enums**: Use bare symbols (`rl/KEY_SPACE`), not function calls (`(rl/KEY_SPACE)`)
- **Flecs init**: Use `(cpp/box (fl/ecs_mini))` - header require + boxing
- **Flecs fini**: Needs cpp/raw wrapper due to opaque_box extraction
- **Minimize cpp/raw**: Only use for opaque_box extraction, callbacks, complex loops

## What's Next

1. Add more Flecs functionality via header require (components, systems, queries)
2. Consider adding `cpp/unbox-opaque` helper to jank to avoid cpp/raw wrappers
3. Test WASM build with updated flecs integration
