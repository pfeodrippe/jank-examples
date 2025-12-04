# Moving Jolt Wrappers to Pure Jank

**Date**: 2025-12-04

## Summary

Successfully moved all jolt C++ wrapper functions to pure jank code using the `["jolt_c.h" :as jolt :scope ""]` header require.

## What Was Done

### 1. Removed `g_jolt_world` and `jolt-world-ptr`

Originally tried to move `g_jolt_world` from C++ to jank as `jolt-world-ptr`, but this wasn't necessary since the world is passed around as an opaque_box. The global pointer tracking was removed entirely.

### 2. Moved C++ Jolt Wrapper Functions to Pure Jank

The following functions were rewritten from C++ `cpp/raw` to pure jank using header requires:

- `create-world` - calls `jolt/jolt_world_create`, wraps in `cpp/box`
- `destroy-world!` - calls `jolt/jolt_world_destroy`
- `create-sphere` - calls `jolt/jolt_body_create_sphere`
- `create-floor` - calls `jolt/jolt_body_create_box`
- `set-velocity!` - calls `jolt/jolt_body_set_velocity`
- `step!` - calls `jolt/jolt_world_step`
- `optimize!` - calls `jolt/jolt_world_optimize_broad_phase`
- `num-bodies` - calls `jolt/jolt_world_get_num_bodies`
- `num-active` - calls `jolt/jolt_world_get_num_active_bodies`
- `reset-simulation!` - pure jank version

### 3. Key Pattern for Jolt Functions

```clojure
;; Get raw void* from opaque_box
(cpp/opaque_box_ptr world-box)

;; Call jolt C API directly
(jolt/jolt_world_step raw-ptr dt 1)

;; Wrap void* in opaque_box for jank interop
(cpp/box raw-ptr)
```

## Issues Fixed

### 1. Type Inference Error for `v->p`
- Error: `v->p: Cannot infer type, please provide it explicitly`
- Fix: Use explicit type: `(u/v->p cpp/nullptr "void*")`
- But ultimately removed `jolt-world-ptr` since it wasn't needed

### 2. Unresolved Symbol `p<-`
- Error: `Unable to resolve symbol 'vybe.util/p<-'`
- Issue: `p<-` macro doesn't exist in vybe.util!
- Fix: Removed all `u/p<-` usages since global pointer tracking wasn't needed

### 3. Function Order Issue
- Error: `Unable to resolve symbol 'num-bodies'`
- Issue: Functions defined after use in `native-draw-imgui-panel`
- Fix: Moved jolt wrapper functions to BEFORE `native-draw-imgui-panel`

### 4. Type Mismatch: Real vs Integer
- Error: `invalid object type (expected real found integer)`
- Issue: Float params like `0` instead of `0.0`
- Fix: Use float literals: `0.0`, `50.0`, `-0.5`

### 5. Type Mismatch: Integer vs Boolean
- Error: `invalid object type (expected integer found boolean)`
- Issue: C API expects integers for bool params, not jank `true`/`false`
- Fix: Use `0`/`1` instead of `false`/`true`

## Key Learnings

1. **vybe.util only has `v->p` and `p->v`**, not `p<-` for setting values
2. **Float literals matter**: `0` is integer, `0.0` is float for C interop
3. **Boolean to int conversion**: Use `0`/`1` for C bool parameters via header requires
4. **Function order matters**: Define functions before they're used in jank
5. **opaque_box pattern**: Use `cpp/box` to wrap raw pointers, `cpp/opaque_box_ptr` to unwrap

## Files Modified

- `/Users/pfeodrippe/dev/something/src/my_integrated_demo.jank`

## Commands Used

```bash
./run_integrated.sh 2>&1 | tail -100
```

## Physics Not Stepping Fix (Additional Issue)

After getting the demo to compile, physics wasn't stepping - balls were visible but not moving.

### Cause
The `jolt_world_step` C function expects `float` parameter:
```c
void jolt_world_step(void* world_ptr, float delta_time, int collision_steps);
```

But jank's `real` type is `double` (64-bit), not `float` (32-bit). When passing a jank double to a C function expecting float via header requires, the type mismatch caused the physics step to receive garbage/zero values.

### Solution
Added a C++ wrapper that handles the conversion:
```cpp
inline void jolt_step_wrapper(jank::runtime::object_ref world_box, double dt, int64_t collision_steps) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(world_box);
    void* world_ptr = o->data.data;
    jolt_world_step(world_ptr, (float)dt, (int)collision_steps);
}
```

And updated jank `step!` to use it:
```clojure
(defn step!
  "Step the physics simulation."
  [w dt]
  (cpp/jolt_step_wrapper w dt 1))
```

### Key Learning
**Type conversion between jank and C via header requires**:
- jank `real` (native_real) = C++ `double` (64-bit)
- C `float` = 32-bit
- When C function expects `float` but jank passes `double`, need explicit C++ wrapper for conversion
- Same issue likely applies to other float parameters in Jolt/Raylib APIs

## What's Next

The demo is fully working with pure jank jolt wrappers. The main remaining C++ code is:
- ImGui rendering (requires `cpp/raw` for rlgl/imgui integration)
- Entity position tracking (could potentially be moved to pure jank)
- View transformation helpers (could be moved to jank)
- `jolt_step_wrapper` - required for double->float conversion
