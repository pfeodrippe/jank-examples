# iOS JIT "Expected Symbol Found Nil" Investigation

## Date: 2026-01-02

## Error Summary

When running `make ios-jit-sim-run`, the following error occurs:

```
[loader] Phase 2 - Calling entry function for: vybe.util
Exception caught while destructing binding_scope
[jank] Error calling -main: invalid object type (expected symbol found nil)
```

## Investigation Steps Completed

### 1. Previous Fix Already Applied

The fix from `ai/20260102-001-jit-ios-binding-scope-error-analysis.md` has been applied:
- **loader.cpp:1216** - Changed from `object_ref (*)()` to `void (*)()`
- **Two-phase loading** implemented - load all objects first, then call entry functions
- **Core modules list** expanded to include `clojure.string`, `clojure.set`, etc.

### 2. Compile Server Binary Status

- Rebuilt at 14:15 on Jan 2, 2026 (same time as jank binary)
- Contains `clojure.string` in core_modules() set (verified via strings command)
- However, user logs don't show "Skipping core module: clojure.string"

### 3. Error Analysis

The error flow is:
1. `jank_load_vybe_util$loading__()` entry function is called
2. Inside the entry function, a `binding_scope` is created (likely from `require` or `refer` calls)
3. An exception "invalid object type (expected symbol found nil)" is thrown
4. The `binding_scope` destructor catches it and prints "Exception caught while destructing binding_scope"
5. The exception is swallowed, but state is corrupted
6. Later, when calling `-main`, the corrupted state causes failure

### 4. Root Cause Analysis

The error `expect_object<obj::symbol>` with nil happens in **`clojure/core_native.cpp:230`**:

```cpp
for(auto it = core_map->fresh_seq(); !it.is_nil(); it = it->next_in_place())
{
  auto const entry = it->first();
  auto const entry_sym = expect_object<obj::symbol>(runtime::first(entry));  // <- ERROR HERE
  ...
}
```

This is called from the `refer` function when iterating over a namespace's mappings.

**The question is: How does a nil key end up in a namespace's vars map?**

### 5. Likely Root Cause: Uninitialized Var Refs in Entry Function

Looking at the generated code for vybe.util (`/tmp/jank-debug-dep-vybe_util.cpp`):

```cpp
extern "C" void jank_load_vybe_util$loading__(){
  jank_ns_intern_c("vybe.util$loading__");
  jank_ns_set_symbol_counter("clojure.core", 84151);

  // Var refs are initialized using placement new
  new (&vybe::util::clojure_core_SLASH_dissoc_83660)
    jank::runtime::var_ref(jank::runtime::__rt_ctx->intern_var("clojure.core/dissoc").expect_ok());
  ...
```

The entry function initializes many `var_ref` objects. If any of these initializations fail or produce unexpected results, it could corrupt the namespace state.

### 6. Potential Issues Identified

#### Issue A: `clojure.string` Not Being Skipped by Compile Server

The log shows these modules being skipped:
```
[compile-server] Skipping core module: clojure.core-native
[compile-server] Skipping core module: jank.perf-native
[compile-server] Skipping core module: jank.compiler-native
[compile-server] Skipping core module: native
[compile-server] Skipping core module: cpp
```

But NOT `clojure.string`, even though it's in `core_modules()`.

**Possible explanation**: `clojure.string` might not appear in `modules_to_compile` because it's not detected as a dependency by the compile server's dependency resolution.

#### Issue B: Namespace Mapping Corruption

When `refer 'clojure.core` is called during vybe.util initialization, it iterates over `clojure.core`'s vars map. If this map has a nil key, the error occurs.

A nil key could appear if:
1. A var was interned with an empty/nil name
2. Memory corruption during initialization
3. Race condition (unlikely since entry functions are called sequentially)

#### Issue C: Initialization Order

The entry function does:
1. `jank_ns_intern_c("vybe.util$loading__")` - intern the loading namespace
2. `jank_ns_set_symbol_counter("clojure.core", 84151)` - set clojure.core's counter
3. Initialize var refs for clojure.core functions
4. Call dynamic_call for require, refer, etc.

If step 3 or 4 fails partway through, it could leave the namespace in an inconsistent state.

## Commands Run

```bash
# Check jank binary timestamp
ls -la /Users/pfeodrippe/dev/jank/compiler+runtime/build/jank

# Check recent commits
cd /Users/pfeodrippe/dev/jank/compiler+runtime && git log --oneline -5

# Verify clojure.string in core_modules
grep -n "clojure.string" src/cpp/jank/runtime/module/loader.cpp

# Check compile server contains clojure.string
strings build/compile-server | grep -E "clojure\.(string|set|walk)"
```

## Key Files to Investigate Further

1. **`/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/ns.cpp`**
   - `intern_var` and `intern_owned_var` - how vars are added to namespace
   - Look for paths where nil could be inserted

2. **`/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp`**
   - Lines 220-240: `refer` implementation
   - Line 874: Similar code for WASM setup

3. **`/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`**
   - Lines 940-1020: Dependency resolution for iOS JIT
   - Check why `clojure.string` isn't being skipped

## Next Steps

### Immediate Actions

1. **Add debug logging** in `clojure/core_native.cpp` around the refer function to print each entry before calling `expect_object<symbol>`:
   ```cpp
   for(auto it = core_map->fresh_seq(); !it.is_nil(); it = it->next_in_place())
   {
     auto const entry = it->first();
     auto const first_elem = runtime::first(entry);
     std::cout << "[refer debug] entry type: " << object_type_str(first_elem->type) << std::endl;
     if(first_elem->type == object_type::nil) {
       std::cout << "[refer debug] NIL KEY FOUND - skipping" << std::endl;
       continue;
     }
     auto const entry_sym = expect_object<obj::symbol>(first_elem);
     ...
   }
   ```

2. **Check compile server dependency resolution** - Why isn't `clojure.string` appearing as a dependency to skip?

3. **Verify clojure.core state on iOS** before vybe.util loading - Add logging to check if clojure.core's vars map is clean

### Longer Term

1. Consider adding validation in `ns::intern_var` to reject nil symbols
2. Add more robust error handling in the refer/require paths
3. Investigate the dependency resolution in compile server

## Hypothesis

The most likely root cause is that **a nil key is somehow being inserted into clojure.core's vars map during iOS JIT initialization**, possibly due to:

1. An uninitialized var_ref in the generated code
2. A failed `intern_var` call that corrupts the namespace
3. Missing or incomplete initialization of clojure.core on the iOS side before the JIT modules are loaded

The fix would be to:
1. Add defensive checks in the refer/require code to skip nil entries
2. Find and fix the root cause of nil insertion into the namespace
