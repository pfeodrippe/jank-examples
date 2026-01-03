# iOS JIT Nil Symbol Debugging

## Date: 2026-01-02

## Summary

Continued investigation into the iOS JIT "invalid object type (expected symbol found nil)" error. Added better error handling and debugging output.

## Error Analysis

The error occurs during namespace loading in `vybe.util`:
```
[loader] what(): invalid object type (expected symbol found nil); value=nil
[loader] current *ns*: vybe.util
```

This happens when:
1. `vybe.util` is loaded
2. The `ns` macro calls `(refer 'clojure.core)` to refer all clojure.core symbols
3. When iterating over namespace mappings, a nil key is encountered
4. `try_object<symbol>(nil)` throws "expected symbol found nil"

## Root Cause Hypothesis

The nil key in namespace mappings is likely caused by **ABI mismatch** between:
- **Compile server** (macOS): Compiles code with certain defines
- **iOS app**: Has different struct layouts due to different IMMER defines

When the compile server iterates over `ns->get_mappings()`, the immer hash map's internal structure is different from what the iOS runtime expects, causing garbage/nil values to appear during iteration.

## Changes Made

### 1. Improved `refer` function error message (`core_native.cpp:311-326`)

Added defensive check with context:
```cpp
object_ref refer(object_ref const current_ns, object_ref const sym, object_ref const var)
{
  auto const ns_obj = expect_object<runtime::ns>(current_ns);
  auto const var_obj = expect_object<runtime::var>(var);

  /* Defensive check with better error message for debugging nil symbol issues */
  if(sym.is_nil())
  {
    throw std::runtime_error(
      "refer: sym is nil for var " + var_obj->n->name->to_code_string()
      + "/" + var_obj->name->to_code_string() + " in namespace " + ns_obj->name->to_code_string());
  }

  ns_obj->refer(try_object<obj::symbol>(sym), var_obj).expect_ok();
  return jank_nil();
}
```

### 2. Enhanced `expect_object` debugging (`rtti.hpp:66-101`)

Added memory dump and exception throwing:
```cpp
if(o->type != T::obj_type)
{
  std::cerr << "[EXPECT_OBJECT] type mismatch: got " << static_cast<int>(o->type) << " ("
            << object_type_str(o->type) << ")"
            << " expected " << static_cast<int>(T::obj_type) << " ("
            << object_type_str(T::obj_type) << ")"
            << " ptr=" << static_cast<void const *>(o.data) << "\n";

  /* Dump memory around the pointer to help debug ABI mismatches */
  std::cerr << "[EXPECT_OBJECT] Memory dump (first 64 bytes at ptr): ";
  auto const bytes = reinterpret_cast<unsigned char const *>(o.data);
  for(size_t i = 0; i < 64 && bytes; ++i)
  {
    std::cerr << std::hex << static_cast<int>(bytes[i]) << " ";
  }
  std::cerr << std::dec << "\n";

  /* Throw to get a stack trace in debugger */
  throw std::runtime_error(
    "[EXPECT_OBJECT] type mismatch: got " + std::to_string(static_cast<int>(o->type))
    + " (" + object_type_str(o->type) + ") expected " + std::to_string(static_cast<int>(T::obj_type))
    + " (" + object_type_str(T::obj_type) + ")");
}
```

## Previous Session Fixes (from 20260102-ios-jit-ci-fix-and-immer-abi.md)

Added IMMER defines to compile server (`persistent_compiler.hpp`):
```cpp
driver_args_storage.push_back("-DJANK_IOS_JIT=1");
driver_args_storage.push_back("-DIMMER_HAS_LIBGC=1");
driver_args_storage.push_back("-DIMMER_TAGGED_NODE=0");
```

## Debug Steps

### Using lldb with iOS Simulator

1. In Xcode, set breakpoints at crash locations
2. Run app in Debug mode (not Release)
3. When exception is thrown, debugger will pause
4. Use `bt` to see backtrace
5. Use `p` to print variables

### Key Locations to Debug

- `server.hpp:188` - `expect_object<symbol>` on mappings iteration
- `core_native.cpp:314` - `refer` function call
- `rtti.hpp:76` - `expect_object` type check

## Next Steps

1. **Verify jank rebuild completed** - Build was interrupted, need full rebuild
2. **Restart compile server** - Ensure it picks up the new code
3. **Clean iOS app build** - Remove derived data and rebuild
4. **Check for other ABI-affecting defines** - May need `JANK_TARGET_IOS=1` in compile server too

## Commands

```bash
# Rebuild jank completely
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Sync headers to iOS project
cd /Users/pfeodrippe/dev/something
make ios-jit-sync-includes

# Clean and rebuild iOS app
# In Xcode: Product -> Clean Build Folder
# Or: rm -rf ~/Library/Developer/Xcode/DerivedData/SdfViewerMobile*
```

## Files Changed

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp`
   - Enhanced `refer` function with nil check

2. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/rtti.hpp`
   - Added memory dump to `expect_object`
   - Made `expect_object` throw exception on mismatch
   - Added `<string>` include
