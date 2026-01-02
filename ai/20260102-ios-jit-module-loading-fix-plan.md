# iOS JIT Module Loading Crash Fix - Implementation Plan

**Date**: 2026-01-02

## Problem Summary

iOS AOT works fine, but iOS JIT crashes after loading 2 modules (vybe.sdf.math, vybe.sdf.state). The crash occurs when loading the 3rd module.

## Root Cause Analysis

### The Core Issue: Immediate Load + Execute Without Dependency Awareness

The JIT module loading code in `loader.cpp:1098-1203` does this for each module:

```cpp
for(auto const &mod : response.modules) {
  // 1. Load object into JIT
  __rt_ctx->jit_prc.load_object(data, size, entry_symbol);

  // 2. IMMEDIATELY call entry function
  auto fn_ptr = (object_ref (*)())(sym_result.expect_ok());
  fn_ptr();  // <<< CRASH POINT
}
```

### Why This Fails

**JIT Generated Code Structure:**
```cpp
namespace vybe::sdf::math {
  // GLOBAL uninitialized var_refs
  jank::runtime::var_ref vybe_sdf_math_SLASH_sqrt_75269;
  jank::runtime::var_ref vybe_sdf_math_SLASH_sin_75270;
  // ... hundreds more
}

extern "C" void jank_load_vybe_sdf_math$loading__() {
  // Placement new initializes globals
  new (&vybe::sdf::math::vybe_sdf_math_SLASH_sqrt_75269)
    jank::runtime::var_ref(jank::runtime::__rt_ctx->intern_var(...));
}
```

**The Problem Sequence:**

1. Module 1 (math) loads → entry function runs → initializes its globals ✓
2. Module 2 (state) loads → entry function runs → initializes its globals ✓
3. Module 3 (util) loads → **ORC JIT resolves symbols**
   - Module 3's code REFERENCES Module 2's globals
   - Those globals were initialized by Module 2's entry function
   - But if symbol resolution happens BEFORE entry functions complete...
   - **OR** if Module 3's entry function is called before Module 2's globals are fully registered in the symbol table...
   - **CRASH!**

### Why AOT Works

**AOT Generated Code Structure:**
```cpp
struct jank_load_vybe_sdf_math : jank::runtime::obj::jit_function {
  // Member variables with constructor initialization
  jank::runtime::var_ref const vybe_sdf_math_sqrt_1054;

  jank_load_vybe_sdf_math() :
    vybe_sdf_math_sqrt_1054{ jank::runtime::__rt_ctx->intern_var(...) }
  {  }
};
```

AOT doesn't have the global variable problem because:
1. All modules are statically linked into one binary
2. All symbols are resolved at link time
3. Initialization happens in controlled order via `jank_aot_init()`

## Detailed Investigation

### Key Files

| File | Purpose |
|------|---------|
| `loader.cpp:1098-1203` | iOS JIT module loading loop |
| `loader.cpp:1177-1186` | Object file loading per module |
| `loader.cpp:1189-1197` | Entry function finding and calling |
| `processor.cpp:453-483` | ORC JIT `load_object()` implementation |
| `processor.cpp:533-551` | Symbol resolution via ORC + dlsym |

### Current Loading Flow (Broken)

```
Server Response: [module1, module2, module3, ...]
                      ↓
For each module:
  1. load_object(data) → ORC JIT resolves symbols immediately
  2. find_symbol(entry) → Get entry function pointer
  3. fn_ptr()           → Execute initialization
     ↓
  Next module...
```

The problem: When loading module3, ORC JIT's `addObjectFile()` immediately tries to resolve ALL symbols in module3's object file. If module3 references module2's globals, those might not be in the JIT symbol table yet (even though module2's entry function was called).

### Evidence from Compile Server

Looking at the compile order:
```
[compile-server] Compiling transitive dependency: vybe.sdf.math
[compile-server] Compiling transitive dependency: vybe.sdf.state
[compile-server] Compiling transitive dependency: vybe.util         ← Likely crash
[compile-server] Compiling transitive dependency: vybe.sdf.shader
...
```

The modules are compiled in dependency order, but the problem is on the CLIENT side during loading.

## Solution: Two-Phase Loading

### The Fix

Change the loading loop from:
```
For each module:
  Load + Execute immediately
```

To:
```
Phase 1: Load ALL object files into ORC JIT
Phase 2: Call ALL entry functions in dependency order
```

### Why This Works

1. **Phase 1** adds all symbols to ORC JIT's symbol table
2. When each object is added, its symbols are registered (even if not initialized)
3. **Phase 2** calls entry functions, which initialize the globals
4. By the time Module 3's entry function runs, Modules 1 & 2 are fully initialized

### Implementation Location

**File**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp`
**Function**: The iOS JIT module loading section (around line 1098-1203)

### Code Changes

```cpp
// BEFORE (broken):
for(auto const &mod : response.modules) {
  __rt_ctx->jit_prc.load_object(
    (char const *)mod.object_data.data(),
    mod.object_data.size(),
    mod.entry_symbol
  );

  auto sym_result = __rt_ctx->jit_prc.find_symbol(mod.entry_symbol);
  auto fn_ptr = (object_ref (*)())(sym_result.expect_ok());
  fn_ptr();  // Immediate execution
}

// AFTER (fixed):
// Phase 1: Load all objects
std::vector<std::pair<std::string, void*>> entry_functions;
for(auto const &mod : response.modules) {
  __rt_ctx->jit_prc.load_object(
    (char const *)mod.object_data.data(),
    mod.object_data.size(),
    mod.entry_symbol
  );

  // Store entry function for Phase 2
  auto sym_result = __rt_ctx->jit_prc.find_symbol(mod.entry_symbol);
  if(sym_result.is_ok()) {
    entry_functions.push_back({mod.name, sym_result.expect_ok()});
  }
}

// Phase 2: Execute all entry functions
for(auto const &[name, fn_addr] : entry_functions) {
  auto fn_ptr = (object_ref (*)())fn_addr;
  fn_ptr();
}
```

### Additional Considerations

1. **Error Handling**: If any object fails to load in Phase 1, we should abort before Phase 2
2. **Logging**: Add debug logging to show which phase we're in
3. **Native Aliases**: The native alias registration should happen after all modules load but before entry functions run

## Step-by-Step Implementation

### Step 1: Find the exact loading code

Look for the iOS JIT module loading loop in `loader.cpp`. The key identifiers are:
- `load_object`
- `compiled_module`
- `entry_symbol`
- `JANK_IOS_JIT` or iOS-specific guards

### Step 2: Implement two-phase loading

1. Create a vector to store entry function pointers
2. Loop 1: Load all objects, collect entry functions
3. Loop 2: Call all entry functions

### Step 3: Test

```bash
rm -rf target/  # Clear cache
make ios-jit-sim-run
```

### Step 4: Handle edge cases

- Empty response (no modules)
- Single module (should still work)
- Failed object loading (abort early)

## Alternative Solutions (if Two-Phase Doesn't Work)

### Alternative 1: Lazy Symbol Resolution

Configure ORC JIT to use lazy binding instead of eager binding. This defers symbol resolution until first use.

```cpp
// In processor initialization
// Use LazyCallThroughManager for deferred resolution
```

### Alternative 2: Explicit Symbol Registration

Before loading each object, pre-register all symbols it exports with placeholder addresses, then update after loading.

### Alternative 3: Change JIT Code Generation

Make JIT generated code more like AOT:
- Use member variables instead of global namespace variables
- Initialize in constructor instead of placement new

This is a larger change and should be avoided if possible.

## Testing Plan

1. Clear cache: `rm -rf target/`
2. Run test: `make ios-jit-sim-run`
3. Verify all modules load
4. Verify `-main` runs successfully
5. Verify app displays correctly

## Files to Modify

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp`
   - Implement two-phase loading

## Success Criteria

- All 9 modules load without crash
- `-main` function executes
- App displays correctly
- No memory corruption or crashes

## Rollback Plan

If the fix causes other issues:
1. Revert the loader.cpp changes
2. Test on macOS JIT to ensure no regression
3. Investigate alternative solutions
