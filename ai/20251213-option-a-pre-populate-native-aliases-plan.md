# Plan: Pre-populate native_aliases During AOT Initialization (Option A)

## Goal

Eliminate the `runtime/invalid-cpp-eval` error in standalone AOT binaries by pre-populating the `native_aliases` map BEFORE module load functions run. When `register_native_header` executes, `add_native_alias` will return `false` (alias already exists), skipping the JIT compilation entirely.

## Current Flow (Broken)

```
AOT Binary Starts
    │
    ▼
jank_init_with_pch() initializes runtime
    │
    ▼
jank_load_vybe_sdf_render() runs
    │
    ▼
ns macro expansion executes register_native_header()
    │
    ▼
add_native_alias() returns true (map is empty)
    │
    ▼
eval_cpp_string("#include <sdf_engine.hpp>") ──► FAILS (header not bundled)
```

## Proposed Flow (Fixed)

```
AOT Binary Starts
    │
    ▼
jank_init_with_pch() initializes runtime
    │
    ▼
jank_aot_init_native_aliases() ◄── NEW: Pre-populates native_aliases for all namespaces
    │
    ▼
jank_load_vybe_sdf_render() runs
    │
    ▼
ns macro expansion executes register_native_header()
    │
    ▼
add_native_alias() returns false (alias already exists!)
    │
    ▼
JIT compilation skipped ──► SUCCESS
```

## Data Structures

### native_alias (existing)

```cpp
// include/cpp/jank/runtime/ns.hpp:39
struct native_alias
{
  jtl::immutable_string header;            // e.g., "vulkan/sdf_engine.hpp"
  jtl::immutable_string include_directive; // e.g., "<vulkan/sdf_engine.hpp>"
  jtl::immutable_string scope;             // e.g., "sdfx"
};
```

### AOT Native Alias Record (new)

```cpp
// Struct for storing alias info in AOT binary
struct aot_native_alias_record
{
  char const* ns_name;           // "vybe.sdf.render"
  char const* alias_name;        // "sdfx"
  char const* header;            // "vulkan/sdf_engine.hpp"
  char const* include_directive; // "<vulkan/sdf_engine.hpp>"
  char const* scope;             // "sdfx"
};
```

## Implementation Steps

### Step 1: Track Native Aliases During Compilation

**File**: `src/cpp/jank/runtime/context.cpp`

During `compile_string` or module compilation, after a namespace is fully loaded, capture its native_aliases.

```cpp
// Add to context class
native_unordered_map<jtl::immutable_string,
                     native_vector<std::tuple<jtl::immutable_string, ns::native_alias>>>
  aot_native_aliases_by_ns;

// After loading a module, capture its native aliases
void capture_native_aliases_for_aot(jtl::immutable_string const& ns_name)
{
  auto const ns = find_ns(ns_name);
  if(ns.is_some())
  {
    auto aliases = ns->native_aliases_snapshot();
    // Store for later AOT generation
    aot_native_aliases_by_ns[ns_name] = std::move(aliases);
  }
}
```

### Step 2: Generate Native Alias Initialization Code

**File**: `src/cpp/jank/aot/processor.cpp`

In `gen_entrypoint`, generate a static array of native alias records and a function to initialize them.

```cpp
// In gen_entrypoint(), after generating module load externs:

// 1. Generate the static data array
sb(R"(
namespace {
  struct aot_native_alias_record {
    char const* ns_name;
    char const* alias_name;
    char const* header;
    char const* include_directive;
    char const* scope;
  };

  static constexpr aot_native_alias_record aot_native_aliases[] = {
)");

// Iterate over all captured native aliases
for(auto const& [ns_name, aliases] : __rt_ctx->aot_native_aliases_by_ns)
{
  for(auto const& [alias_sym, alias_data] : aliases)
  {
    util::format_to(sb,
      R"(    {{ "{}", "{}", "{}", "{}", "{}" }},)" "\n",
      ns_name,
      alias_sym->name,
      alias_data.header,
      alias_data.include_directive,
      alias_data.scope);
  }
}

sb(R"(
    { nullptr, nullptr, nullptr, nullptr, nullptr }  // Sentinel
  };
}
)");

// 2. Declare the C API function
sb(R"(extern "C" void jank_aot_init_native_aliases(aot_native_alias_record const* records);)" "\n");
```

### Step 3: Add C API Function to Initialize Native Aliases

**File**: `src/cpp/jank/c_api.cpp`

```cpp
void jank_aot_init_native_aliases(aot_native_alias_record const* records)
{
  if(!records) return;

  for(; records->ns_name != nullptr; ++records)
  {
    // Find or create the namespace
    auto ns = __rt_ctx->intern_ns(records->ns_name);

    // Create the alias symbol
    auto alias_sym = make_box<obj::symbol>(records->alias_name);

    // Create the native_alias struct
    ns::native_alias alias_data{
      records->header,
      records->include_directive,
      records->scope
    };

    // Add to the namespace (won't JIT because we're just registering)
    ns->add_native_alias(alias_sym, std::move(alias_data));
  }
}
```

### Step 4: Call Initialization in Generated main()

**File**: `src/cpp/jank/aot/processor.cpp`

In the generated `main()` function, call the init function BEFORE loading modules:

```cpp
sb(entry_signature);
sb(R"(
{
  auto const fn{ [](int const argc, char const **argv) {
    jank_load_clojure_core_native();
    jank_load_clojure_core();
    jank_module_set_loaded("/clojure.core");
    jank_load_jank_compiler_native();
    jank_load_jank_nrepl_server_asio();
    jank_module_set_loaded("/jank.nrepl-server.asio");

    // NEW: Initialize native aliases BEFORE loading user modules
    jank_aot_init_native_aliases(aot_native_aliases);

)");

// Then load user modules...
for(auto const &it : *modules_rlocked)
{
  util::format_to(sb, "{}();\n", module::module_to_load_function(it));
}
```

### Step 5: Ensure register_native_header Doesn't Re-JIT

The existing logic in `register_native_header` already handles this:

```cpp
// core_native.cpp - existing code
auto const added = ns_obj->add_native_alias(...).expect_ok();
if(added)  // <-- This will be FALSE because we pre-populated!
{
  // JIT compilation happens here - but won't execute!
  auto const res{ __rt_ctx->eval_cpp_string(include_code) };
  ...
}
```

No changes needed here - the pre-population makes `add_native_alias` return `false`.

## Files to Modify

| File | Change |
|------|--------|
| `include/cpp/jank/runtime/context.hpp` | Add `aot_native_aliases_by_ns` member |
| `src/cpp/jank/runtime/context.cpp` | Capture native aliases after module load |
| `include/cpp/jank/c_api.hpp` | Declare `jank_aot_init_native_aliases` |
| `src/cpp/jank/c_api.cpp` | Implement `jank_aot_init_native_aliases` |
| `src/cpp/jank/aot/processor.cpp` | Generate alias data and init call |

## Alternative: Simpler Approach Using Existing Infrastructure

Instead of a new array, we could generate direct calls to a simpler C API:

```cpp
// In generated main(), before loading user modules:
jank_register_native_alias_aot("vybe.sdf.render", "sdfx",
                                "vulkan/sdf_engine.hpp",
                                "<vulkan/sdf_engine.hpp>",
                                "sdfx");
jank_register_native_alias_aot("vybe.sdf.screenshot", "sdfx",
                                "vulkan/sdf_engine.hpp",
                                "<vulkan/sdf_engine.hpp>",
                                "sdfx");
// ... etc
```

This is more verbose but simpler to implement - no struct definitions needed.

## Edge Cases to Handle

### 1. Same Header in Multiple Namespaces

Multiple namespaces can require the same header. Each namespace gets its own alias entry. The pre-population handles this correctly - each NS gets its alias registered.

### 2. Namespace Doesn't Exist Yet

When `jank_aot_init_native_aliases` runs, some namespaces may not exist yet. We need to `intern_ns` to create them.

### 3. Order Dependencies

Native aliases must be registered BEFORE module load functions run. The generated code ensures this by calling `jank_aot_init_native_aliases` first.

### 4. Incremental/Partial AOT

If mixing AOT and JIT code:
- AOT modules: aliases pre-populated, no JIT needed
- JIT modules: aliases not pre-populated, JIT runs normally

This works correctly because `add_native_alias` only returns `true` for new aliases.

## Testing Plan

1. **Build standalone app**: `make sdf-standalone`
2. **Verify no warnings/errors**: App should start without JIT compilation warnings
3. **Verify functionality**: All `sdfx::*` calls should work
4. **Test nREPL**: Adding new header requires via nREPL should still JIT compile (since those aliases aren't pre-populated)

## Complexity Assessment

| Aspect | Complexity | Notes |
|--------|------------|-------|
| Data tracking | Low | Just store what we already have |
| Code generation | Medium | String formatting, but straightforward |
| C API addition | Low | Simple function |
| Testing | Medium | Need to verify various scenarios |
| **Overall** | **Medium** | ~200-300 lines of code changes |

## Benefits

1. **Zero runtime overhead**: No JIT compilation attempted, no file checks, no warnings
2. **Semantically correct**: Aliases exist because they were compiled, not assumed
3. **Works with partial AOT**: JIT modules still work normally
4. **No changes to existing APIs**: `register_native_header` works unchanged

## Risks

1. **Binary size**: Each alias adds ~100 bytes to binary (negligible)
2. **Namespace creation order**: Need to create NS before load function runs (handled)
3. **Forgetting to capture aliases**: Need to hook into module loading correctly

## Estimated Effort

- Implementation: 4-6 hours
- Testing: 2-3 hours
- Total: ~1 day

## Comparison with Current Fix (Warning-based)

| Aspect | Current Fix | Option A |
|--------|-------------|----------|
| Correctness | Assumes AOT | Knows AOT |
| Runtime overhead | File check or warning | None |
| Code complexity | 1 line change | ~250 lines |
| False positives | Possible | None |
| Maintenance | Low | Medium |

## Recommendation

Option A is the right fix if you want jank to handle AOT header requires correctly. It's more work but eliminates the class of problems entirely.

For your immediate needs, the current warning-based fix works. Option A would be a good contribution to jank itself.
