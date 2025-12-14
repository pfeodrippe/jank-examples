# jank Compiler Fix: Void-returning C++ functions now return nil

## Summary

Fixed jank compiler so void-returning C++ functions automatically return `nil` everywhere (let bodies, def, etc.).

## Problem

```clojure
(let [result (let []
               (vkCmdBindPipeline ...))]  ;; void as let return - crashed!
  ...)
```

Error: `cannot form a reference to 'void'` - tried to create `option<void>`.

## Smart Solution

Instead of fixing case-by-case, use the existing `non_void_expression_type()` function that converts void → `object_ref`.

### Key Changes in `processor.cpp`:

1. **let/letfn codegen** - Use `non_void_expression_type` instead of `expression_type`:
```cpp
auto const last_expr_type{ cpp_util::non_void_expression_type(
  expr->body->values[expr->body->values.size() - 1]) };
```

2. **cpp_call codegen** - Call void function, then assign nil:
```cpp
";jank::runtime::object_ref const {}{ jank::runtime::jank_nil };"
```

## Key Insight

jank already has `non_void_expression_type()` in `cpp_util.cpp`:
```cpp
/* Void is a special case which gets turned into nil, but only in some circumstances. */
jtl::ptr<void> non_void_expression_type(expression_ref const expr)
{
  auto const type{ expression_type(expr) };
  if(Cpp::IsVoid(type))
  {
    return untyped_object_ptr_type();
  }
  return type;
}
```

We just needed to use it in the right places!

## Test

```clojure
;; test/jank/cpp/call/global/pass-void-in-let-body.jank
(let [result (let [_ (assert (= 0 counter))]
               (increment))]  ;; void call as let body return
  (assert (nil? result))
  :success)
```

## Commands

```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime
./bin/compile
./build/jank run test/jank/cpp/call/global/pass-void-in-let-body.jank  # :success
```
