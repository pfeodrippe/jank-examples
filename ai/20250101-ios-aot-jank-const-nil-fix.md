# iOS AOT jank_const_nil Infinite Recursion Fix - 2025-01-01

## Summary
Fixed an infinite recursion crash in iOS AOT builds caused by `oref<obj::nil>::erase()` calling `jank_const_nil()`.

## The Problem
The iOS AOT simulator app was crashing immediately on launch with a stack overflow. The crash report showed:
```
0   SdfViewerMobile-AOT-Sim.debug.dylib  jank::runtime::jank_nil() + 0
1   SdfViewerMobile-AOT-Sim.debug.dylib  jank_const_nil + 12
2-510  SdfViewerMobile-AOT-Sim.debug.dylib  jank_const_nil + 16 (repeated)
```

## Root Cause
In `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/oref.hpp`, the `oref<obj::nil>` template specialization had an `erase()` method that called `jank_const_nil()`:

```cpp
template <>
struct oref<obj::nil>
{
  // ...
  oref<object> erase() const noexcept
  {
    return { std::bit_cast<object *>(jank_const_nil()) };  // PROBLEM!
  }
  // ...
};
```

Meanwhile, in `c_api_wasm.cpp`, `jank_const_nil()` was implemented as:
```cpp
jank_object_ref jank_const_nil()
{
  return jank_nil().erase().data;  // Calls jank_nil().erase()
}
```

This created an infinite recursion:
1. `jank_const_nil()` is called
2. `jank_const_nil()` calls `jank_nil().erase()`
3. `jank_nil()` returns an `oref<obj::nil>` (which is `obj::nil_ref`)
4. `.erase()` on `oref<obj::nil>` calls `jank_const_nil()` again
5. Infinite loop!

## The Fix
Changed `oref<obj::nil>::erase()` to return the pointer directly without calling `jank_const_nil()`:

```cpp
oref<object> erase() const noexcept
{
  /* Don't call jank_const_nil() here - it would cause infinite recursion
   * since jank_const_nil() calls jank_nil().erase().
   * Since nil::base is the only data member, we can reinterpret_cast directly. */
  return { reinterpret_cast<object *>(data) };
}
```

Note: We use `reinterpret_cast` instead of `&data->base` because `obj::nil` is incomplete (only forward declared) at the point where `oref.hpp` is included.

## Files Changed
- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/oref.hpp` (line ~451)

## Build & Test Commands
```bash
# Rebuild jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Build and run iOS AOT simulator
make ios-aot-sim-run
```

## Verification
After the fix, the iOS AOT simulator app launched successfully and displayed the 3D SDF viewer with a hand model.

## Related Issues
This fix is related to the iOS AOT linker fixes from earlier today (see `20250101-ios-aot-linker-fixes.md`).
