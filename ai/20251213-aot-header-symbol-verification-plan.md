# Deep Investigation: AOT Header Symbol Verification

## Executive Summary

The current fix (warn instead of throw on header JIT failure) works but is not semantically correct. It assumes symbols are "AOT compiled" when JIT fails, but this assumption may be wrong. We need a proper mechanism to verify symbol availability.

## The Problem Space

### What Happens Today

1. **JIT Mode (dev)**:
   - `register_native_header` is called
   - `add_native_alias` returns true (alias is new)
   - `eval_cpp_string("#include <header>")` succeeds
   - Header is parsed, symbols become available
   - Code can call `sdfx::init()`, etc.

2. **AOT Mode (standalone)**:
   - During compilation: Header WAS included and compiled into binary
   - At runtime: `register_native_header` is called during module load
   - `add_native_alias` returns true (namespace state starts fresh)
   - `eval_cpp_string("#include <header>")` fails (header not bundled)
   - Current fix: warn and continue
   - Code can still call functions because they're in the binary

### The Deep Problem

**Why aren't the symbols found as symbols?**

Looking at `sdf_engine.hpp`:
```cpp
namespace sdfx {
inline bool g_sigint_received = false;
inline void sigint_handler(int sig) { ... }
// All functions are inline!
}
```

Because all functions are `inline`, they get **inlined at call sites** during compilation. There are no standalone `sdfx::*` symbols in the binary - the code is directly embedded where it's called.

This means:
- We CANNOT use `find_symbol("sdfx::init")` to verify availability
- The symbols don't exist as addressable entities
- The code IS there, just not as separate symbols

### When The Current Fix Is Wrong

1. **JIT mode with missing header**: If header file is missing in dev mode, JIT fails, we warn, but symbols truly don't exist. Code will crash when calling `sdfx::*`.

2. **nREPL adding new header require**: User evaluates `(require '["new_header.h" :as nh])` in nREPL. Header is missing, we warn, but `nh` symbols are completely unavailable.

3. **Partially AOT compiled code**: Main code is AOT, but user is JIT compiling new code that needs the header. Warning is misleading.

## Investigation: Symbol Resolution in jank

### How `find_symbol` Works

```cpp
// jit/processor.cpp:442
jtl::string_result<void *> processor::find_symbol(jtl::immutable_string const &name) const
{
  if(auto symbol{ interpreter->getSymbolAddress(name.c_str()) })
  {
    return symbol.get().toPtr<void *>();
  }
  return err(util::format("Failed to find symbol: '{}'", name));
}
```

This uses Clang's `getSymbolAddress` which searches:
- JIT compiled code
- Loaded object files
- Loaded dynamic libraries
- Main executable (if symbols are exported)

**Problem**: Inline functions have no address - they're not symbols!

### How AOT Code Is Structured

When jank compiles `(sdfx/init "path")`:
1. Analyzer looks up `sdfx` native alias → finds `sdf_engine.hpp`
2. Codegen generates: `sdfx::init("path")`
3. The `#include <sdf_engine.hpp>` is added to generated C++
4. Clang compiles: inlines the function body directly
5. Final binary contains the inlined code, no `sdfx::init` symbol

### Why `native_aliases` Starts Fresh

The `ns` object (namespace) is created at runtime. Its `native_aliases` map starts empty because:
- Namespace state isn't serialized during AOT compilation
- Each runtime creates namespaces fresh
- `add_native_alias` is called by AOT code during module load

This is why `add_native_alias` returns `true` even for "already compiled" headers.

## Proposed Solutions

### Option 1: Check If Header File Exists (Simplest)

**Idea**: If header file is available on disk, try to JIT compile. If not, assume AOT.

```cpp
object_ref register_native_header(...) {
  // ... alias_data setup ...

  auto const added = ns_obj->add_native_alias(...).expect_ok();
  if(added)
  {
    // Check if header file exists in include paths
    bool header_available = check_header_exists(alias_data.header);

    if(header_available)
    {
      auto const include_code{ util::format("#include {}\n", include_arg) };
      auto const res{ __rt_ctx->eval_cpp_string(include_code) };
      if(res.is_err())
      {
        throw res.expect_err(); // Real error - file exists but won't compile
      }
    }
    else
    {
      // Header not available - assume AOT compiled
      // Just register the alias without JIT compiling
      util::println("[info] Header '{}' not found - assuming AOT compiled", include_arg);
    }
  }
  return jank_nil;
}
```

**Pros**:
- Simple to implement
- Matches user intent (standalone apps don't bundle headers)
- Distinguishes "file missing" from "compile error"

**Cons**:
- Doesn't verify symbols actually exist
- Could silently fail if header exists but is broken

**Implementation Complexity**: Low

### Option 2: AOT Runtime Flag

**Idea**: Add a flag to jank runtime indicating "running AOT compiled code".

```cpp
// In context or cli options
bool is_aot_runtime = false;

// Set by AOT compiled main()
extern "C" void jank_set_aot_runtime() {
  is_aot_runtime = true;
}

// In register_native_header
if(added && !is_aot_runtime)
{
  // JIT compile only in non-AOT mode
  auto const res{ __rt_ctx->eval_cpp_string(include_code) };
  // ...
}
```

**Pros**:
- Explicit control over behavior
- Clear semantics
- Can be used for other AOT-specific behavior

**Cons**:
- Requires jank changes
- Need to ensure flag is set correctly by AOT binaries
- Doesn't help with partial AOT scenarios

**Implementation Complexity**: Medium

### Option 3: Track AOT Compiled Headers (Most Correct)

**Idea**: During AOT compilation, record which headers were compiled. Check at runtime.

```cpp
// During AOT compilation, accumulate:
std::set<std::string> aot_compiled_headers;

// Serialize into binary:
extern const char* jank_aot_headers[] = { "sdf_engine.hpp", "imgui.h", nullptr };

// At runtime:
bool is_header_aot_compiled(const std::string& header) {
  for(int i = 0; jank_aot_headers[i]; ++i) {
    if(jank_aot_headers[i] == header) return true;
  }
  return false;
}

// In register_native_header
if(added)
{
  if(is_header_aot_compiled(alias_data.header))
  {
    // Skip JIT, header was AOT compiled
  }
  else
  {
    // JIT compile
    auto const res{ __rt_ctx->eval_cpp_string(include_code) };
    if(res.is_err()) throw res.expect_err();
  }
}
```

**Pros**:
- Semantically correct
- No false positives
- Works with partial AOT

**Cons**:
- Significant implementation effort
- Need to modify AOT codegen
- Need to handle namespace boundaries

**Implementation Complexity**: High

### Option 4: Try-Use Pattern (Deferred Verification)

**Idea**: Register the alias, don't JIT immediately. Only error when actually USING a symbol that doesn't exist.

```cpp
// In register_native_header
auto const added = ns_obj->add_native_alias(...).expect_ok();
if(added)
{
  // Try to JIT compile, but don't fail immediately
  auto const res{ __rt_ctx->eval_cpp_string(include_code) };
  if(res.is_err())
  {
    // Mark alias as "unverified" - symbols might be AOT or might be missing
    ns_obj->mark_alias_unverified(alias);
  }
}

// When code tries to call sdfx::init():
// 1. If symbol exists (JIT or AOT): call succeeds
// 2. If symbol missing: error with clear message
```

**Pros**:
- Defers decision to actual usage
- No false positives at registration time
- Works with inline functions (call will work or fail clearly)

**Cons**:
- Error happens later, harder to diagnose
- Need to modify symbol resolution
- Doesn't provide early warning

**Implementation Complexity**: Medium-High

### Option 5: Hybrid - File Check + Compile Attempt

**Idea**: Check if file exists. If yes, compile. If compile fails, provide detailed error.

```cpp
if(added)
{
  bool header_found = check_header_in_paths(alias_data.header);

  if(header_found)
  {
    auto const res{ __rt_ctx->eval_cpp_string(include_code) };
    if(res.is_err())
    {
      // Header exists but failed to compile - real error
      throw make_error(
        kind::runtime_invalid_cpp_eval,
        util::format("Header '{}' found but failed to compile", include_arg),
        source,
        res.expect_err()  // Include underlying error
      );
    }
  }
  else
  {
    // Header not found - check if we're in a context where this is OK
    if(__rt_ctx->allow_missing_headers || is_aot_context())
    {
      util::println("[info] Header '{}' not found, assuming AOT compiled", include_arg);
    }
    else
    {
      throw make_error(
        kind::runtime_invalid_cpp_eval,
        util::format("Header '{}' not found in include paths", include_arg),
        source
      );
    }
  }
}
```

**Pros**:
- Distinguishes file-not-found from compile-error
- Configurable behavior
- Good error messages

**Cons**:
- More complex logic
- Need to implement header path searching
- Need to add config option

**Implementation Complexity**: Medium

## Recommended Approach

### Short Term (Your Current Situation)

**Option 1: Check If Header File Exists**

This is the pragmatic choice because:
1. It matches your actual use case (standalone without headers)
2. It's simple to implement
3. It distinguishes "missing file" from "broken file"
4. It won't silently fail if header exists but is broken

### Medium Term (jank Improvement)

**Option 2 + Option 5: AOT Flag + File Check**

1. Add `is_aot_runtime` flag to jank
2. AOT compiled binaries set this flag
3. In AOT mode: skip header JIT for all headers (they're all compiled)
4. In JIT mode: check if file exists, compile if found, error if not

### Long Term (Proper Solution)

**Option 3: Track AOT Compiled Headers**

This is the semantically correct solution but requires significant work.

## Implementation Plan for Option 1

### Step 1: Add header path checking utility

```cpp
// In jank/util/path.hpp or similar
bool header_exists_in_paths(
  const std::string& header,
  const std::vector<std::string>& include_paths
);
```

### Step 2: Get include paths from JIT processor

The JIT processor already knows include paths (from `-I` flags). Expose this.

### Step 3: Modify register_native_header

```cpp
object_ref register_native_header(...)
{
  // ... existing code ...

  if(added)
  {
    auto const include_paths = __rt_ctx->jit_prc.get_include_paths();
    bool header_found = header_exists_in_paths(alias_data.header, include_paths);

    if(header_found)
    {
      auto const include_code{ util::format("#include {}\n", include_arg) };
      auto const res{ __rt_ctx->eval_cpp_string(include_code) };
      if(res.is_err())
      {
        // File exists but failed to compile - this is a real error
        throw res.expect_err();
      }
    }
    else
    {
      // File not found - assume symbols are AOT compiled or in loaded libraries
      // This is expected for standalone apps that don't bundle development headers
    }
  }
  return jank_nil;
}
```

### Step 4: Test scenarios

1. **Dev mode with headers**: Should JIT compile normally
2. **Standalone without headers**: Should skip JIT, work via AOT
3. **Dev mode with missing header**: Should error clearly
4. **Standalone trying to add new header via nREPL**: Should error (file not found)

## Open Questions

1. **What about headers in JARs?** Need to check JAR contents too.

2. **System headers like `<iostream>`?** These always exist, should always JIT.

3. **Relative vs absolute paths?** Need to handle both in path searching.

4. **Cached JIT state?** If header was JIT compiled previously in session, don't recompile.

5. **Multiple namespaces using same header?** Only compile once, share state.

## Files to Modify

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp`
   - `register_native_header` function

2. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/jit/processor.cpp`
   - Add `get_include_paths()` method

3. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/jit/processor.hpp`
   - Declare `get_include_paths()`

4. (Optional) `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/util/path.cpp`
   - Add `header_exists_in_paths()` utility

## Conclusion

The current warning-based fix works for your immediate need but isn't correct in general. The proper fix involves checking if the header file exists before deciding whether to JIT compile. This matches user intent (standalone apps don't have dev headers) while still catching real errors (header exists but broken).

The deeper issue is that jank doesn't distinguish between "header unavailable" and "header broken" - it treats both as "JIT failed". Adding file existence checking solves this without major architectural changes.
