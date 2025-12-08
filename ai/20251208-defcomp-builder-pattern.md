# Session: defcomp Builder Pattern - Unlimited Fields + Name Mangling

**Date**: 2025-12-08
**Status**: COMPLETED - All tests pass

## What Was Changed

### Problem 1: Fixed field count
The previous implementation used separate C++ helper functions for 1-4 fields (`vybe_register_struct_1` through `vybe_register_struct_4`), which was limiting and "bad code" per user feedback.

### Solution 1: Builder Pattern
Implemented a **builder pattern** that supports unlimited fields (up to 32, which is Flecs' internal limit for `ecs_struct_desc_t.members`):

```cpp
struct VybeStructBuilder {
  ecs_world_t* world;
  ecs_struct_desc_t desc;
  int member_idx;
};

VybeStructBuilder* vybe_struct_begin(ecs_world_t* w, const char* name);
void vybe_struct_add_member(VybeStructBuilder* b, const char* name, ecs_entity_t type);
ecs_entity_t vybe_struct_end(VybeStructBuilder* b);
```

### Problem 2: Unqualified component names
Component names like "Position" would conflict if defined in multiple namespaces.

### Solution 2: Namespace-qualified mangled names
Added `mangle-name` function to convert namespace names to Flecs-safe identifiers:
- Replaces `.` with `_`
- Replaces `-` with `_`
- Separates namespace and symbol with `__`

Example: `vybe.type-test/Position` → `vybe_type_test__Position`

```clojure
(defn mangle-char [c]
  (cond
    (= c \.) \_
    (= c \-) \_
    :else c))

(defn mangle-name [ns-str sym-str]
  (let [mangled-ns (apply str (map mangle-char (str ns-str)))]
    (str mangled-ns "__" sym-str)))
```

The `defcomp` macro now automatically includes the namespace:
```clojure
(defmacro defcomp [sym fields]
  `(def ~sym (make-comp ~(mangle-name *ns* sym) ~fields)))
```

### Key Technique: Boxing Native Pointers

To capture a native pointer (`VybeStructBuilder*`) in a jank closure (like `doseq`), we need to box it:

```clojure
(defn register-comp! [world comp]
  (let [w (vf/world-ptr world)
        builder (cpp/box (cpp/vybe_struct_begin w (:name comp)))]
    (doseq [field (:fields comp)]
      (cpp/vybe_struct_add_member
       (cpp/unbox (cpp/type "VybeStructBuilder*") builder)
       (:name field)
       (flecs-type-id (:type field))))
    (cpp/vybe_struct_end (cpp/unbox (cpp/type "VybeStructBuilder*") builder))))
```

## Commands Run

```bash
./run_tests.sh
# Output:
# Testing vybe.flecs-test
# Ran 9 tests containing 10 assertions.
# 0 failures, 0 errors.
#
# Testing vybe.type-test
# Ran 6 tests containing 27 assertions.
# 0 failures, 0 errors.
```

## Key Learnings

1. **Native pointers can't be captured directly in jank closures** - use `cpp/box` to wrap them first

2. **`cpp/unbox` requires type annotation** - use `(cpp/unbox (cpp/type "T*") boxed-value)`

3. **Builder pattern is better than fixed-arity functions** - allows iterating with `doseq` instead of hardcoded field counts

4. **Flecs type globals exist** - Access `fl/FLECS_IDecs_f32_tID_` etc. directly instead of using `ecs_id(T)` macro (which uses token pasting and doesn't work at runtime)

5. **Flecs entity names must be simple** - No `.` or `/` characters, must mangle to `_` and `__`

6. **`clojure.string/replace` not implemented in jank** - Use `(apply str (map f s))` pattern instead

## Files Modified

- `src/vybe/type.jank` - Builder pattern + name mangling
- `test/vybe/type_test.jank` - Updated expected names to mangled format

## What's Next

- Component data access in `with-query` (reading/writing field values)
- Constructor functions that accept jank hash maps for component instantiation
- Investigate jank macro aliasing issues (full namespace qualification needed in macro expansion)
