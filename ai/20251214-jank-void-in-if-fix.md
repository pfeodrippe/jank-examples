# Fix: Void Functions in `if` Branches

## Problem

jank was generating invalid C++ code when both branches of an `if` expression called void-returning functions:

```clojure
(if condition
  (void-fn-a)
  (void-fn-b))
```

Generated invalid C++:
```cpp
void if_tmp{ };  // ERROR: variable has incomplete type 'void'
```

## Root Cause

In `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp`, line 1345:

```cpp
auto const expr_type{ cpp_util::expression_type(expr->then) };
```

This returned `void` when the then-branch was a void function call, which was then used to declare the temporary variable.

## Fix

Changed to use `non_void_expression_type` which returns `object_ref` for void types:

```cpp
auto const expr_type{ cpp_util::non_void_expression_type(expr->then) };
```

This generates valid C++:
```cpp
object_ref if_tmp{ };  // Valid!
```

The void function calls already return handles to `jank_nil`, so assignments work correctly.

## Test Added

`/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/call/global/pass-void-in-if-branches.jank`

```clojure
(cpp/raw "namespace jank::cpp::call::global::fail_void_in_if_branches
          {
            int counter{ 0 };
            void increment() { counter++; }
            void decrement() { counter--; }
          }")

(let [condition true
      result (if condition
               (cpp/jank.cpp.call.global.fail_void_in_if_branches.increment)
               (cpp/jank.cpp.call.global.fail_void_in_if_branches.decrement))]
  (assert (nil? result))
  (assert (= 1 cpp/jank.cpp.call.global.fail_void_in_if_branches.counter))
  :success)
```

## Commands Used

```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile
./bin/test
```

## Cleanup Applied to something project

After fixing the compiler, cleaned up workarounds in the something project:

1. **`src/vybe/sdf/events.jank`** - Changed from:
   ```clojure
   (do (when has-shift-mod (sdfx/request_redo))
       (when (not has-shift-mod) (sdfx/request_undo)))
   ```
   To proper `if`:
   ```clojure
   (if has-shift-mod
     (sdfx/request_redo)
     (sdfx/request_undo))
   ```

2. **`src/debug_test.jank`** - Changed from:
   ```clojure
   (let* [_ (rl/BeginScissorMode 1.5 2.5 100.5 50.5)]
     nil)
   ```
   To direct call:
   ```clojure
   (rl/BeginScissorMode 1.5 2.5 100.5 50.5)
   ```

## What's Next

The migration of event handling from C++ to jank (Phase 1) is complete.

## Files Modified

- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` - Line 1345
- `/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/call/global/pass-void-in-if-branches.jank` (new)
