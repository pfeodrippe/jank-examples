# Plan: Implementing `:tag` Type Hints for Native Return Types in jank

**Date**: 2024-12-07

## Problem Statement

Currently in jank, when a function returns a native C++ type (like `ecs_world_t*`), the caller must use `cpp/unbox` with an explicit type every time:

```clojure
;; Current situation - verbose and repetitive
(defn world-ptr [world]
  (cpp/unbox (cpp/type "ecs_world_t*") world))

;; Every call site:
(fl/ecs_new (cpp/unbox (cpp/type "ecs_world_t*") (world-ptr world)))
```

We want to enable `:tag` metadata on function definitions to declare return types:

```clojure
;; Goal - declare once, use everywhere
(defn ^{:tag "ecs_world_t*"} world-ptr [world]
  (cpp/unbox (cpp/type "ecs_world_t*") world))

;; Call sites can use the return directly (compiler knows the type)
(fl/ecs_new (world-ptr world))
```

## Existing Infrastructure Analysis

### What Already Exists

1. **`var_deref->tag_type` field** (`include/cpp/jank/analyze/expr/var_deref.hpp:27`)
   - Already stores C++ type string when available
   - Set from local frame's tracked types

2. **`tag_to_cpp_type()` function** (`src/cpp/jank/analyze/cpp_util.cpp`)
   - Converts `:tag` metadata values to C++ type strings
   - Supports keywords (`:i32` → `"int"`) and strings (`"ecs_world_t*"` → `"ecs_world_t*"`)

3. **`:arities` metadata handling** (`src/cpp/jank/analyze/processor.cpp:1900-1950`)
   - Already parses per-arity metadata like `:unboxed-output?`
   - Structure: `{:arities {1 {:unboxed-output? true}}}`

4. **`expression_type()` function** (`src/cpp/jank/analyze/cpp_util.cpp`)
   - Infers C++ type from expressions
   - Returns `native_persistent_string` or `none` for boxed types

### What's Missing

1. **Reading `:tag` from function metadata** - Need to extract `:tag` from defn metadata
2. **Propagating tag to call sites** - When calling a tagged function, the result type should be known
3. **Codegen using tag type** - Generate appropriate C++ code based on return type

## Implementation Plan

### Phase 1: Metadata Extraction (processor.cpp)

**Location**: `src/cpp/jank/analyze/processor.cpp`

**Changes needed**:

1. In `analyze_call()` when handling function calls (~line 1900-2000):
   ```cpp
   // After resolving the var being called
   if(auto const var_deref = boost::get<expr::var_deref<expression>>(&call->source_expr->data))
   {
     // Check if var has :tag metadata
     auto const var = var_deref->var;
     auto const meta = var->meta.unwrap();
     if(meta != nullptr)
     {
       auto const tag = get(meta, __rt_ctx->intern_keyword("tag").expect_ok());
       if(tag != obj::nil::nil_const())
       {
         // Store the tag type on the call expression
         call->return_tag_type = cpp_util::tag_to_cpp_type(tag);
       }
     }
   }
   ```

2. In `analyze_defn()` when processing function metadata (~line 2200+):
   - Extract `:tag` from function metadata
   - Store it on the function expression for later use
   - Also support per-arity tags in `:arities` map

**Files to modify**:
- `src/cpp/jank/analyze/processor.cpp`

### Phase 2: Expression Data Structures

**Location**: `include/cpp/jank/analyze/expr/`

**Changes needed**:

1. Add `return_tag_type` field to `call.hpp`:
   ```cpp
   struct call : gc
   {
     // ... existing fields ...
     option<native_persistent_string> return_tag_type;
   };
   ```

2. Add `return_tag_type` field to `function.hpp` (for defn):
   ```cpp
   struct function : gc
   {
     // ... existing fields ...
     option<native_persistent_string> return_tag_type;  // From :tag metadata
   };
   ```

**Files to modify**:
- `include/cpp/jank/analyze/expr/call.hpp`
- `include/cpp/jank/analyze/expr/function.hpp`

### Phase 3: Update expression_type()

**Location**: `src/cpp/jank/analyze/cpp_util.cpp`

**Changes needed**:

In `expression_type()`, add handling for call expressions with tag types:
```cpp
option<native_persistent_string> expression_type(expression_ptr const expr)
{
  return boost::apply_visitor(
    util::make_visitor(
      // ... existing cases ...
      [](expr::call<expression> const &c) -> option<native_persistent_string>
      {
        if(c.return_tag_type.is_some())
        {
          return c.return_tag_type;
        }
        return none;
      },
      // ... rest ...
    ),
    expr->data);
}
```

**Files to modify**:
- `src/cpp/jank/analyze/cpp_util.cpp`

### Phase 4: Code Generation

**Location**: `src/cpp/jank/codegen/processor.cpp`

**Changes needed**:

1. When generating code for a call expression:
   - If `return_tag_type` is set, use it for the result type
   - Generate appropriate cast/conversion if needed

2. Example codegen change:
   ```cpp
   void gen_call(expr::call<expression> const &call)
   {
     // If this call has a known return type, we can use it
     if(call.return_tag_type.is_some())
     {
       // Generate: static_cast<ecs_world_t*>(call_result)
       // or just use the type directly if calling a native function
     }
   }
   ```

**Files to modify**:
- `src/cpp/jank/codegen/processor.cpp`

### Phase 5: Var Metadata Propagation

**Location**: `src/cpp/jank/runtime/var.cpp` and related

**Changes needed**:

When a var is defined with `:tag` metadata, ensure it's accessible at analysis time when the var is dereferenced.

This may already work if metadata is properly stored on the var during `defn` processing.

## Tests to Add

### Unit Tests

**Location**: `compiler+runtime/test/cpp/jank/`

1. **Tag metadata parsing tests** (`test/cpp/jank/analyze/tag_test.cpp`):
   ```cpp
   TEST_CASE("tag_to_cpp_type converts keywords")
   {
     CHECK(tag_to_cpp_type(keyword("i32")) == "int");
     CHECK(tag_to_cpp_type(keyword("f64")) == "double");
   }

   TEST_CASE("tag_to_cpp_type passes through strings")
   {
     CHECK(tag_to_cpp_type(make_box("ecs_world_t*")) == "ecs_world_t*");
     CHECK(tag_to_cpp_type(make_box("MyClass*")) == "MyClass*");
   }
   ```

2. **Function tag extraction tests**:
   ```cpp
   TEST_CASE("defn with :tag extracts return type")
   {
     auto result = analyze("(defn ^{:tag \"int*\"} foo [] nil)");
     auto fn = get_function(result);
     CHECK(fn.return_tag_type.unwrap() == "int*");
   }
   ```

### Integration Tests

**Location**: `compiler+runtime/test/jank/`

1. **Basic tag usage** (`test/jank/tag_hints.jank`):
   ```clojure
   ;; Test that :tag metadata is recognized
   (defn ^{:tag :i32} get-count [] 42)

   ;; Should work without explicit unboxing
   (assert (= 42 (get-count)))
   ```

2. **Native pointer tags** (`test/jank/native_tag_hints.jank`):
   ```clojure
   (ns test.native-tags
     (:require ["test_header.h" :as t]))

   (defn ^{:tag "test_struct_t*"} make-struct []
     (t/create_struct))

   ;; Use without explicit type annotation
   (defn use-struct []
     (let [s (make-struct)]
       (t/do_something s)))  ; Compiler knows s is test_struct_t*
   ```

3. **Per-arity tags** (`test/jank/arity_tags.jank`):
   ```clojure
   (defn ^{:arities {1 {:tag :i32}
                     2 {:tag :f64}}}
     compute
     ([x] (int x))
     ([x y] (double (* x y))))
   ```

### REPL Tests

**Location**: `compiler+runtime/test/jank/repl/`

1. Test that tag metadata is preserved through REPL redefinition
2. Test that calling tagged functions works in JIT mode

## Migration Path

### Backward Compatibility

- Functions without `:tag` continue to work as before (boxed return)
- `:tag` is opt-in enhancement
- No breaking changes to existing code

### Documentation

Add to jank docs:
1. How to use `:tag` for return type hints
2. Supported type formats (keywords like `:i32`, strings like `"MyType*"`)
3. Per-arity tag configuration

## Potential Challenges

1. **Multiple return paths**: If a function has multiple return statements with different types, validation is needed

2. **Type mismatch errors**: Need good error messages when declared tag doesn't match actual return type

3. **Interop with unboxed params**: Ensure `:tag` works well with `:unboxed-params?` and `:unboxed-output?`

4. **Template types**: Handling complex C++ template types in tag strings

## Priority Order

1. **Phase 1 & 2**: Metadata extraction and data structures (foundation)
2. **Phase 3**: expression_type() update (enables type inference)
3. **Phase 4**: Codegen updates (makes it actually work)
4. **Phase 5**: Edge cases and polish
5. **Tests**: Parallel to each phase

## Estimated Complexity

- **Metadata extraction**: Medium - need to navigate jank's metadata structures
- **Data structure changes**: Low - straightforward additions
- **expression_type() update**: Low - pattern matching on new case
- **Codegen**: Medium-High - depends on how return values are currently handled
- **Tests**: Medium - need to set up native code for integration tests

## Commands to Run

```bash
# Setup build environment
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++

# Build after changes
./bin/compile

# Run tests
./bin/test

# Run specific test file
./bin/test --gtest_filter="*tag*"
```

## Related Files Reference

Key files in jank compiler:
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/analyze/processor.cpp` - Main analysis
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/analyze/cpp_util.cpp` - Type utilities
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` - Code generation
- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/analyze/expr/` - Expression types
- `/Users/pfeodrippe/dev/jank/compiler+runtime/ai/20251203-002-tag-type-hints-plan.md` - Previous related plan
