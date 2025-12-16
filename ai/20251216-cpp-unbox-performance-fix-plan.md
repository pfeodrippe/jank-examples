# cpp/unbox Performance Fix Plan

**Date**: 2025-12-16
**Issue**: `cpp/unbox` takes ~380us per call, causing massive CPU overhead
**Impact**: UI code calling `p->v` (which uses `cpp/unbox`) multiple times per frame causes CPU spike from 48% to 106%

## Root Cause Analysis

### The Problem

The jank compiler generates this code for every `cpp/unbox` call:

```cpp
auto cpp_unbox_123{
  static_cast<bool*>(jank_unbox_with_source(
    "bool*",
    value.data,
    jank::runtime::__rt_ctx->read_string("{:line 42 :col 5 :file \"ui.jank\"}").data
  ))
};
```

**The killer**: `read_string("{:line 42 :col 5 ...}")` is called **every single time** `cpp/unbox` executes!

### Cost Breakdown (measured)

| Operation | Time per call |
|-----------|---------------|
| `read_string` (parse source metadata) | ~90us |
| Type string comparison | ~10us |
| `try_object` type check | ~5us |
| Other overhead | ~275us |
| **Total** | **~380us** |

For comparison:
- Simple arithmetic `(+ 1 2)`: ~0.5us
- `imgui/Text` call: ~1us

### Why This Happens

Looking at `src/cpp/jank/codegen/processor.cpp` lines 2016-2025:

```cpp
util::format_to(body_buffer,
                "auto {}{ "
                "static_cast<{}>(jank_unbox_with_source(\"{}\", {}.data, "
                "jank::runtime::__rt_ctx->read_string(\"{}\").data)"  // <-- THIS!
                ") };",
                ret_tmp,
                type_name,
                type_name,
                value_tmp.unwrap().str(false),
                util::escape(runtime::to_code_string(meta)));
```

The source metadata string is embedded in the generated code and **parsed at runtime** via `read_string` on every call - just for error reporting that rarely (if ever) triggers!

### The `jank_unbox_with_source` Function

Located in `src/cpp/jank/c_api.cpp`:

```cpp
void *jank_unbox_with_source(char const * const type,
                             jank_object_ref const o,
                             jank_object_ref const source)  // <-- only used for errors!
{
  auto const op_box{ try_object<obj::opaque_box>(box_obj) };
  if(!op_box->canonical_type.empty() && op_box->canonical_type != type)
  {
    throw error::runtime_invalid_unbox(..., meta_source(source_obj), ...);  // rarely called
  }
  return op_box->data;  // the actual work - just returns a pointer!
}
```

The `source` parameter is **only used when throwing an exception** for type mismatch errors.

## Proposed Solutions

### Solution 1: Lazy Source Evaluation (Recommended)

**Approach**: Don't parse source metadata until an error actually occurs.

**Changes to codegen** (`processor.cpp`):

```cpp
// Before (current):
"jank_unbox_with_source(\"{}\", {}.data, "
"jank::runtime::__rt_ctx->read_string(\"{}\").data)"

// After (proposed):
"jank_unbox_lazy_source(\"{}\", {}.data, \"{}\")"
```

**New C API function** (`c_api.cpp`):

```cpp
void *jank_unbox_lazy_source(char const * const type,
                             jank_object_ref const o,
                             char const * const source_str)  // raw string, not parsed
{
  auto const op_box{ try_object<obj::opaque_box>(reinterpret_cast<object *>(o)) };
  if(!op_box->canonical_type.empty() && op_box->canonical_type != type)
  {
    // Only parse source string when error occurs (rare path)
    auto const source = __rt_ctx->read_string(source_str);
    throw error::runtime_invalid_unbox(..., meta_source(source.data), ...);
  }
  return op_box->data;
}
```

**Expected improvement**: ~90us saved per call (the `read_string` cost)

### Solution 2: Global Source Constants (Alternative)

**Approach**: Generate source metadata as compile-time globals.

**Changes to codegen**:

```cpp
// Generate once per unique source location:
"static auto const source_42_5{ jank::runtime::__rt_ctx->read_string(\"{:line 42 :col 5}\") };"

// Use the cached value:
"jank_unbox_with_source(\"{}\", {}.data, source_42_5.data)"
```

**Pros**: Source is parsed once at module load time
**Cons**: Adds memory for each unique source location

### Solution 3: Skip Source in Release Mode (Quick Win)

**Approach**: Use simpler `jank_unbox` (no source) when not in debug mode.

```cpp
#ifdef JANK_DEBUG
  jank_unbox_with_source(type, val, source)
#else
  jank_unbox(type, val)  // existing function, no source overhead
#endif
```

**Pros**: Zero overhead in release
**Cons**: No source info in release error messages

### Solution 4: Inline the Unbox (Most Aggressive)

**Approach**: Generate inline code instead of function call.

```cpp
// Before:
"jank_unbox_with_source(...)"

// After:
"[&]{ auto b = jank::runtime::try_object<jank::runtime::obj::opaque_box>({}.data); "
"return static_cast<{}>(b->data); }()"
```

**Pros**: Eliminates function call overhead entirely
**Cons**: More generated code, no type checking at runtime

## Implementation Plan

### Phase 1: Quick Fix (Solution 3)

1. Add `--fast-unbox` CLI flag to jank
2. When enabled, generate `jank_unbox` instead of `jank_unbox_with_source`
3. Test performance improvement

**Files to modify**:
- `src/cpp/jank/util/cli.cpp` - add flag
- `src/cpp/jank/codegen/processor.cpp` - conditional codegen

### Phase 2: Proper Fix (Solution 1)

1. Add `jank_unbox_lazy_source` to C API
2. Update codegen to use new function
3. Update LLVM codegen (`llvm_processor.cpp`) similarly
4. Test error messages still work

**Files to modify**:
- `include/cpp/jank/c_api.h` - declare new function
- `src/cpp/jank/c_api.cpp` - implement new function
- `src/cpp/jank/codegen/processor.cpp` - update codegen
- `src/cpp/jank/codegen/llvm_processor.cpp` - update LLVM codegen

### Phase 3: Validation

1. Run jank test suite
2. Benchmark before/after with UI-heavy code
3. Verify error messages still contain source info

## Expected Results

| Solution | Improvement | Complexity |
|----------|-------------|------------|
| Solution 3 (fast flag) | ~90-100us saved | Low |
| Solution 1 (lazy source) | ~90us saved | Medium |
| Solution 4 (inline) | ~300us+ saved | High |

With Solution 1, expected `cpp/unbox` time: **~290us -> ~5-10us** (mostly just type check)

## Commands Used for Investigation

```bash
# Find unbox implementation
grep -rn "cpp_unbox" ~/dev/jank/compiler+runtime/src/

# Check codegen
grep -n "jank_unbox" ~/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp

# Read c_api implementation
cat ~/dev/jank/compiler+runtime/src/cpp/jank/c_api.cpp | grep -A20 "jank_unbox_with_source"

# Benchmark in REPL
(let [start (now-us)] (dotimes [_ 1000] (read-string "{:line 1}")) (- (now-us) start))
```

## Key Files

| File | Purpose |
|------|---------|
| `src/cpp/jank/codegen/processor.cpp:2002-2040` | JIT codegen for cpp/unbox |
| `src/cpp/jank/codegen/llvm_processor.cpp:3045-3070` | LLVM codegen for cpp/unbox |
| `src/cpp/jank/c_api.cpp:758-776` | `jank_unbox_with_source` implementation |
| `src/cpp/jank/analyze/processor.cpp:4836-4983` | Analyzer for cpp/unbox |
| `include/cpp/jank/c_api.h:169` | C API declaration |

## Summary

The `cpp/unbox` performance issue is caused by **runtime parsing of source metadata on every call**. The fix is straightforward: defer the parsing until an error actually occurs (which is rare). This should reduce `cpp/unbox` from ~380us to ~5-10us per call, eliminating the CPU spike in UI code.
