# Investigation: compilation_target::eval vs compilation_target::module

Date: 2026-01-04

## Summary

Investigated the difference between `compilation_target::eval` and `compilation_target::module` in jank's codegen processor, specifically how lifted constants are generated and initialized for each target.

## Key Findings

### 1. Constant Declaration Location

**eval target:**
- Constants are declared as **struct members** inside the jit_function struct
- Uses `const` keyword
- Location: `header_buffer` (struct scope)

**module target:**
- Constants are declared as **namespace-level globals**
- No `const` keyword (needs to be mutable for placement new)
- Location: `module_header_buffer` (namespace scope)

### 2. Constant Initialization Mechanism

**eval target (lines 2681-2708):**
```cpp
if(target == compilation_target::eval)
{
    for(auto const &v : lifted_constants)
    {
        util::format_to(header_buffer, ", {}{", v.second);
        detail::gen_constant(v.first, header_buffer, true);
        util::format_to(header_buffer, "}");
    }
}
```
- Constants initialized in **constructor initializer list**
- Values generated inline
- Struct cannot be constructed without fully initializing constants

**module target (lines 2863-2933):**
```cpp
if(is_module_like)
{
    util::format_to(footer_buffer,
                    "extern \"C\" void {}(){",
                    runtime::module::module_to_load_function(module));
    // ... later ...
    for(auto const &v : lifted_constants)
    {
        util::format_to(footer_buffer,
                        "new (&{}::{}) {}(",
                        ns, v.second, detail::gen_constant_type(v.first, true));
        detail::gen_constant(v.first, footer_buffer, true);
        util::format_to(footer_buffer, ");");
    }
}
```
- Constants initialized via **placement new** in `jank_load_XXX()` function
- Initialization happens ONLY when load function is called
- Globals exist uninitialized until then

### 3. Generated Code Structure

**eval target generates:**
```cpp
namespace jank_ns_foo {
    struct jit_fn_123 : jank::runtime::obj::jit_function {
        object_ref const const_456;  // Struct member

        jit_fn_123() : jank::runtime::obj::jit_function{ ... },
            const_456{ make_box<obj::persistent_vector>(...) }  // Init in ctor
        { }

        object_ref call() final { return const_456; }
    };
}
```

**module target generates:**
```cpp
namespace jank_ns_foo {
    object_ref const_456;  // Namespace global - UNINITIALIZED!

    struct jit_fn_123 : jank::runtime::obj::jit_function {
        jit_fn_123() : jank::runtime::obj::jit_function{ ... } { }
        object_ref call() final { return const_456; }  // May be nil!
    };
}

extern "C" void jank_load_foo() {
    jank_ns_intern_c("foo");
    new (&jank_ns_foo::const_456) object_ref(...);  // Placement new init
    jank_ns_foo::jit_fn_123{ }.call();
}
```

### 4. Root Cause of iOS JIT Nil Constants Issue

The module target has a **temporal gap** between:
1. When globals are declared (at object file load)
2. When globals are initialized (when `jank_load_XXX` runs)

If any code accesses the globals before `jank_load_XXX` fully completes, constants are nil.

Possible iOS JIT scenarios causing nil:
1. Load function not being called at all
2. Load function being called but nested functions execute before constants init
3. Symbol resolution issues causing wrong entry point
4. Global static initialization order issues

## Commands Used

```bash
# Read processor.cpp lines 2595-2940
Read /Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp offset=2595 limit=350

# Search for compilation_target usage
Grep "compilation_target::" in processor.cpp

# Search for lifted_constant usage
Grep "lifted_constant" in processor.cpp
```

## Files Examined

- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`
  - Lines 2500-2550: `declaration_str()` - builds all buffers
  - Lines 2551-2629: `build_header()` - declares constants
  - Lines 2681-2710: Constructor initialization for eval target
  - Lines 2847-2934: `build_footer()` - generates `jank_load_XXX` for module target

- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
  - Lines 1083, 1600: iOS JIT uses `compilation_target::module`

## Next Steps

To fix the iOS JIT nil constants issue:
1. Verify `jank_load_XXX` is actually being called after object file load
2. Check symbol resolution for the load function on iOS
3. Consider if iOS has different static initialization order guarantees
4. Add logging to track when `jank_load_XXX` runs vs when constants are accessed
5. Consider alternative initialization strategy for iOS JIT (e.g., using eval-style member initialization)
