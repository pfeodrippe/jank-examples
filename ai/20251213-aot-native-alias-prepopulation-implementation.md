# Session: AOT Native Alias Pre-population Implementation

## Date: 2025-12-13

## Summary

Implemented Option A from the plan: Pre-populate native_aliases during AOT initialization so that `register_native_header` finds aliases already exist and skips JIT compilation entirely.

## Problem

When running standalone AOT compiled apps, `register_native_header` was trying to JIT compile `#include <header>` directives. This failed because headers aren't bundled in standalone apps, causing `runtime/invalid-cpp-eval` errors.

The previous fix (warning instead of throw) worked but was semantically incorrect - it assumed symbols were "already AOT compiled" without verification.

## Solution: Pre-populate Native Aliases

The proper fix is to pre-populate the `native_aliases` map in each namespace BEFORE module load functions run. When `register_native_header` executes:

1. It calls `add_native_alias()`
2. `add_native_alias()` returns `false` (alias already exists!)
3. JIT compilation is skipped entirely
4. No errors, no warnings, no file checks needed

## Files Modified

### jank compiler (in `/Users/pfeodrippe/dev/jank/compiler+runtime/`)

#### 1. `include/cpp/jank/runtime/ns.hpp`
Added new method declaration:
```cpp
native_unordered_map<obj::symbol_ref, native_alias> native_aliases_map_snapshot() const;
```

#### 2. `src/cpp/jank/runtime/ns.cpp`
Implemented the new method:
```cpp
native_unordered_map<obj::symbol_ref, ns::native_alias> ns::native_aliases_map_snapshot() const
{
  auto locked_native_aliases(native_aliases.rlock());
  return *locked_native_aliases;
}
```

#### 3. `src/cpp/jank/c_api.cpp`
Added new C API function:
```cpp
void jank_register_native_alias(char const * const ns_name,
                                char const * const alias_name,
                                char const * const header,
                                char const * const include_directive,
                                char const * const scope)
{
  /* Pre-populate native aliases for AOT compiled code.
   * When called before module load functions run, this ensures that
   * register_native_header will find the alias already exists and skip
   * JIT compilation (which would fail since headers aren't bundled). */
  auto const ns = __rt_ctx->intern_ns(ns_name);
  auto const alias_sym = make_box<obj::symbol>(alias_name);
  runtime::ns::native_alias alias_data{ header, include_directive, scope };
  ns->add_native_alias(alias_sym, std::move(alias_data)).expect_ok();
}
```

#### 4. `src/cpp/jank/aot/processor.cpp`

Added extern declaration:
```cpp
extern "C" void jank_register_native_alias(char const *ns_name,
                                           char const *alias_name,
                                           char const *header,
                                           char const *include_directive,
                                           char const *scope);
```

Added code generation for native alias registration (in `gen_entrypoint`, after core modules but before user modules):
```cpp
/* Pre-populate native aliases for AOT compiled headers.
 * This ensures that when module load functions call register_native_header,
 * the alias already exists and JIT compilation is skipped. */
{
  auto const namespaces_rlocked{ __rt_ctx->namespaces.rlock() };
  for(auto const &[ns_sym, ns_ref] : *namespaces_rlocked)
  {
    auto const aliases = ns_ref->native_aliases_map_snapshot();
    for(auto const &[alias_sym, alias_data] : aliases)
    {
      util::format_to(sb,
                      R"(jank_register_native_alias("{}", "{}", "{}", "{}", "{}");)" "\n",
                      ns_ref->name->name,
                      alias_sym->name,
                      alias_data.header,
                      alias_data.include_directive,
                      alias_data.scope);
    }
  }
}
```

#### 5. `src/cpp/clojure/core_native.cpp`

Reverted warning-based fix to throw proper error with better location info:
```cpp
if(res.is_err())
{
  auto const alias_sym(try_object<obj::symbol>(alias));
  throw std::runtime_error{ util::format(
    "Failed to JIT compile native header require.\n"
    "  Namespace: {}\n"
    "  Alias: {}\n"
    "  Header: {}\n"
    "  Include directive: {}\n\n"
    "If this is an AOT compiled binary, the header should have been pre-registered.\n"
    "Check that the AOT compilation included all required modules.",
    ns_obj->name->to_string(),
    alias_sym->name,
    runtime::to_string(header),
    include_arg) };
}
```

## Generated Code Example

The AOT entrypoint now includes (before loading user modules):
```cpp
jank_register_native_alias("vybe.sdf.render", "sdfx", "vulkan/sdf_engine.hpp", "<vulkan/sdf_engine.hpp>", "sdfx");
jank_register_native_alias("vybe.sdf.screenshot", "sdfx", "vulkan/sdf_engine.hpp", "<vulkan/sdf_engine.hpp>", "sdfx");
jank_register_native_alias("vybe.sdf.ui", "sdfx", "vulkan/sdf_engine.hpp", "<vulkan/sdf_engine.hpp>", "sdfx");
// ... etc for all namespaces with native header requires
```

## Testing Results

### Before (warning-based fix)
```
[warning] Failed to JIT compile header '<vulkan/sdf_engine.hpp>' - symbols may already be AOT compiled
[warning] Failed to JIT compile header '<vulkan/stb_image_write.h>' - symbols may already be AOT compiled
[warning] Failed to JIT compile header '<vulkan/sdf_engine.hpp>' - symbols may already be AOT compiled
[warning] Failed to JIT compile header '<vulkan/sdf_engine.hpp>' - symbols may already be AOT compiled
Starting embedded nREPL server on 127.0.0.1:5557
...
```

### After (native alias pre-population)
```
Starting embedded nREPL server on 127.0.0.1:5557
...
```

No warnings - native aliases are pre-populated, so JIT compilation is never attempted.

## Commands Used

```bash
# Rebuild jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Rebuild standalone app
make sdf-standalone

# Test
./SDFViewer.app/Contents/MacOS/SDFViewer
```

## Benefits

1. **Zero runtime overhead**: No JIT compilation attempted, no file checks, no warnings
2. **Semantically correct**: Aliases exist because they were compiled, not assumed
3. **Works with partial AOT**: JIT modules still work normally (aliases not pre-populated)
4. **Better error messages**: If JIT does fail, error includes namespace, alias, and header info
5. **No changes to existing APIs**: `register_native_header` works unchanged

## Next Steps

Consider upstreaming this fix to the jank repository as a PR.
