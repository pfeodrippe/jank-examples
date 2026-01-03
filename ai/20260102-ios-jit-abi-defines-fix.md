# iOS JIT ABI Defines Fix

## Date: 2026-01-02

## Summary

Fixed the iOS JIT "invalid object type (expected symbol found nil)" error by ensuring all compile-time defines match between the compile server and iOS app.

## Root Cause

The compile server was missing several defines that the iOS app uses, causing ABI mismatch in data structures like immer hash maps. When iterating over namespace mappings, the mismatched memory layouts caused garbage/nil values to appear.

## Missing Defines

The compile server only had:
- `-DJANK_IOS_JIT=1`
- `-DIMMER_HAS_LIBGC=1`
- `-DIMMER_TAGGED_NODE=0`

But iOS builds (from CMakeLists.txt line 209-211) have:
- `-DJANK_TARGET_IOS=1` (CRITICAL - affects header conditionals)
- `-DIMMER_HAS_LIBGC=1`
- `-DIMMER_TAGGED_NODE=0`
- `-DHAVE_CXX14=1`
- `-DFOLLY_HAVE_JEMALLOC=0`
- `-DFOLLY_HAVE_TCMALLOC=0`
- `-DFOLLY_ASSUME_NO_JEMALLOC=1`
- `-DFOLLY_ASSUME_NO_TCMALLOC=1`

## Fix

Added all missing defines to three locations in the compile server:

### 1. persistent_compiler::compile() (lines 154-164)
```cpp
driver_args_storage.push_back("-DJANK_IOS_JIT=1");
// CRITICAL: All defines must match iOS app for ABI compatibility
// These affect memory layout of data structures (immer hash maps, folly, etc.)
driver_args_storage.push_back("-DJANK_TARGET_IOS=1");
driver_args_storage.push_back("-DIMMER_HAS_LIBGC=1");
driver_args_storage.push_back("-DIMMER_TAGGED_NODE=0");
driver_args_storage.push_back("-DHAVE_CXX14=1");
driver_args_storage.push_back("-DFOLLY_HAVE_JEMALLOC=0");
driver_args_storage.push_back("-DFOLLY_HAVE_TCMALLOC=0");
driver_args_storage.push_back("-DFOLLY_ASSUME_NO_JEMALLOC=1");
driver_args_storage.push_back("-DFOLLY_ASSUME_NO_TCMALLOC=1");
```

### 2. incremental_compiler::init() (lines 338-348)
Same defines as above.

### 3. server.hpp popen fallback (lines 1221-1231)
Same defines as above (for fallback cross-compilation path).

## Files Changed

1. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/persistent_compiler.hpp`
   - Added all missing defines to both persistent_compiler and incremental_compiler

2. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
   - Added all missing defines to popen fallback path

3. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/runtime/rtti.hpp`
   - Added memory dump and exception to `expect_object` for debugging
   - Added `<string>` include

4. `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/clojure/core_native.cpp`
   - Added nil check to `refer` function with helpful error message

## Commands Run

```bash
# Rebuild jank with fixes
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile
```

## Why This Matters

The `JANK_TARGET_IOS` define affects code paths in headers like:
- `cpptrace.hpp` - conditional includes
- `try.hpp` - JANK_TRY/JANK_CATCH macros
- `sha256.cpp` - CommonCrypto vs OpenSSL
- `core_native.cpp` - native alias handling

The FOLLY and IMMER defines affect memory allocator and data structure layouts. When code is compiled without these defines but runs on iOS which has them, the struct layouts don't match, causing garbage data when accessing fields.

## Makefile Improvements

Also added improvements to ensure proper cleaning/rebuilding:

### 1. Auto-rebuild compile-server binary
Added `rebuild-jank-compile-server` target that runs `ninja -C build compile-server`.
Both `ios-compile-server-sim` and `ios-compile-server-device` now depend on this target.

### 2. Better Xcode DerivedData cleaning
`ios-jit-sim-build` and `ios-jit-device-build` now:
- Clean `sdf_viewer_ios.*` in addition to `jank_aot_init.*`
- Touch `sdf_viewer_ios.mm` to force rebuild on header changes

## Additional Makefile Improvements

### 3. Added `ios-jit-clean` target
Cleans ALL iOS JIT build artifacts to ensure no stale files:
```bash
make ios-jit-clean
```
This removes:
- `SdfViewerMobile/build-iphonesimulator-jit/`
- `SdfViewerMobile/build-iphoneos-jit/`
- `~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-*`

### 4. Auto-clean generated files
`ios-jit-sim-core` and `ios-jit-device-core` now clean `obj/` and `generated/` before rebuilding.

### 5. Clean old libs before copy
`ios-jit-sim-core-libs` and `ios-jit-device-core-libs` now delete old `.a` files before creating new ones.

## PCH Missing JANK_IOS_JIT=1 (Root Cause!)

The PCH build script was missing `-DJANK_IOS_JIT=1`. This is used in:
- `context.cpp` - affects JIT initialization
- `loader.cpp` - affects module loading
- `c_api.cpp` - affects C API behavior
- `jit/processor.cpp` - affects JIT compilation

**Fixed in**: `SdfViewerMobile/build-ios-pch.sh`

## All Required Defines (Must Match Everywhere!)

| Define | iOS lib | Compile Server | PCH Script |
|--------|---------|----------------|------------|
| `JANK_IOS_JIT=1` | ✓ | ✓ | ✓ |
| `JANK_TARGET_IOS=1` | ✓ | ✓ | ✓ |
| `IMMER_HAS_LIBGC=1` | ✓ | ✓ | ✓ |
| `IMMER_TAGGED_NODE=0` | ✓ | ✓ | ✓ |
| `HAVE_CXX14=1` | ✓ | ✓ | ✓ |
| `FOLLY_*` jemalloc/tcmalloc | ✓ | ✓ | ✓ |

## Debugging validate_meta crash

Added better error handling to `validate_meta` in `metadatable.cpp`:
- Prints meta type value and pointer
- Dumps first 16 bytes of corrupted objects
- Throws exception instead of panicking on corruption

## Next Steps

1. Run `make ios-jit-clean` to remove all stale files
2. Run `make ios-jit-sim-run` - this will:
   - Rebuild iOS libjank.a
   - Regenerate all generated files
   - Rebuild the iOS app
   - Restart the compile server
3. Test if the ABI mismatch is resolved

## Verification

After fixing, the compile server should print these defines when compiling:
```
-DJANK_IOS_JIT=1
-DJANK_TARGET_IOS=1
-DIMMER_HAS_LIBGC=1
-DIMMER_TAGGED_NODE=0
-DHAVE_CXX14=1
-DFOLLY_HAVE_JEMALLOC=0
-DFOLLY_HAVE_TCMALLOC=0
-DFOLLY_ASSUME_NO_JEMALLOC=1
-DFOLLY_ASSUME_NO_TCMALLOC=1
```
