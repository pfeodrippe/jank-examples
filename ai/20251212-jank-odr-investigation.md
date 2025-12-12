# jank ODR Fix Investigation - Dec 12, 2025

## Summary

Attempted to fix ODR (One Definition Rule) violations in jank standalone builds. The investigation revealed this is a deeper architectural issue in the jank compiler.

## What Was Tried

### 1. Skip JIT compilation of cpp/raw in compile mode (evaluate.cpp)
**Result**: Broke symbol resolution - subsequent `cpp/` interop calls couldn't find symbols.

```cpp
// In evaluate.cpp - eval(expr::cpp_raw_ref)
if(truthy(__rt_ctx->compile_files_var->deref()))
{
  return jank_nil;  // Skip JIT
}
```

**Problem**: `cpp/sdf_sqrt` calls need JIT-compiled symbols to resolve.

### 2. Skip AOT codegen of cpp/raw in compile mode (processor.cpp)
**Result**: Broke AOT compilation - missing includes and other code.

```cpp
// In codegen/processor.cpp - gen(expr::cpp_raw_ref)
if(!truthy(__rt_ctx->compile_files_var->deref()))
{
  util::format_to(deps_buffer, "{}\n", expr->code);
}
```

**Problem**: cpp/raw blocks contain `#include` statements and other necessary code.

### 3. Change `inline` to `static inline` in user code
**Result**: Still causes redefinition errors.

**Problem**: JIT and AOT code are compiled in the same clang interpreter context, so even `static` functions conflict.

### 4. Existing preprocessor guards (`#ifndef JANK_CPP_RAW_XXXX`)
**Result**: Already exist in jank (analyze/processor.cpp:4441-4452) but don't help.

**Problem**: Guards work within a single preprocessing context. JIT and AOT use separate parsing contexts, so guards don't prevent conflicts with already-compiled JIT symbols.

## Root Cause

The fundamental issue is that `jank compile` uses a **hybrid JIT+AOT approach**:

1. **JIT Phase**: Code is JIT-compiled for execution and symbol resolution during module loading
2. **AOT Phase**: The same code is generated as C++ source for the final executable
3. **Both phases use the same clang interpreter**, so the AOT C++ source sees the JIT-compiled symbols

When the AOT-generated C++ is parsed by the same interpreter that already has JIT-compiled cpp/raw code, clang reports redefinition errors.

## Potential Solutions (Not Implemented)

### A. Use separate clang interpreter for AOT compilation
- Would require significant architectural changes
- JIT interpreter for module loading
- Fresh AOT interpreter for generating standalone code

### B. Parse cpp/raw to separate includes from function definitions
- Complex: would need C++ parsing
- Only emit includes in AOT, rely on JIT .o files for definitions

### C. Use weak symbols
- Mark JIT-compiled functions as `__attribute__((weak))`
- AOT functions would override weak symbols
- Would require modifying how cpp/raw is JIT-compiled

### D. External compilation workaround
- User workaround: move cpp/raw code to external .cpp/.h files
- Compile separately and link
- Avoids the issue entirely at the cost of code organization

## Current State

- **JIT mode works perfectly** - all tests pass
- **Standalone mode with cpp/raw is broken** - this is a known jank limitation
- The fix requires changes to jank's core architecture

## Files Modified (reverted)

- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/evaluate.cpp` - Changes reverted
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` - Changes reverted
- `/Users/pfeodrippe/dev/something/src/vybe/type.jank` - Changed `inline` to `static inline` (doesn't fix ODR but is more correct)

## Recommendation

For now, use JIT mode for development. Standalone builds with cpp/raw blocks require jank compiler fixes at the architectural level.
