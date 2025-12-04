# Learning: Using Header Requires for Jolt C API

**Date**: 2024-12-03

## What I Learned

### 1. Creating C Header Files for cpp/raw Functions

When you have C functions defined in a precompiled `.cpp` file (like `jolt_wrapper.cpp`), you can create a header file to expose them to jank's header require system.

**Before:** Functions only accessible via cpp/raw wrappers
**After:** Functions directly callable via header require

### 2. The Pattern for Converting cpp/raw to Header Require

```clojure
;; BEFORE: cpp/raw wrapper needed
(cpp/raw "
inline int64_t jolt_num_bodies(jank::runtime::object_ref w) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(w);
    return jolt_world_get_num_bodies(o->data.data);
}
")
(cpp/jolt_num_bodies w)

;; AFTER: header require + opaque_box_ptr helper
["jolt_c.h" :as jolt :scope ""]
(jolt/jolt_world_get_num_bodies (cpp/opaque_box_ptr w))
```

### 3. Key Components

1. **Header file** (`jolt_c.h`) - Declares the extern "C" functions
2. **Header require** - `["jolt_c.h" :as jolt :scope ""]`
3. **opaque_box_ptr helper** - Extracts `void*` from jank opaque_box (still in cpp/raw, but reusable)

### 4. The opaque_box_ptr Pattern

```cpp
// Minimal cpp/raw helper - reusable for any opaque_box
inline void* opaque_box_ptr(jank::runtime::object_ref box) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(box);
    return o->data.data;
}
```

This single helper can be used for ALL opaque_box extractions, making it a good investment.

## Files Created

1. **`/Users/pfeodrippe/dev/something/vendor/jolt_c.h`**
   - C header with all Jolt wrapper function declarations
   - Includes: world management, shape creation, body creation, body manipulation

## Files Modified

1. **`/Users/pfeodrippe/dev/something/src/my_integrated_demo.jank`**
   - Added header require: `["jolt_c.h" :as jolt :scope ""]`
   - Updated `native-draw-imgui-panel` to use `jolt/jolt_world_get_num_bodies`
   - Updated `num-bodies` function
   - Removed `jolt_num_bodies` wrapper from cpp/raw

## What's Next

1. **Convert more Jolt wrappers** - Same pattern can be applied to:
   - `jolt_num_active` → `jolt/jolt_world_get_num_active_bodies`
   - `jolt_step` → `jolt/jolt_world_step`
   - `jolt_optimize` → `jolt/jolt_world_optimize_broad_phase`
   - And others...

2. **Test the changes** - Run `./run_integrated.sh` to verify the demo works

3. **Consider creating similar headers for other C APIs** - Any extern "C" functions in precompiled code can follow this pattern

## Key Insight

The cpp/raw block is only needed for:
- The `opaque_box_ptr` helper (extracts void* from jank objects)
- Complex C++ code (templates, lambdas, ODR-safe globals)
- Functions that need jank type conversions

Simple C function calls can go through header requires, making the code cleaner and more maintainable.
