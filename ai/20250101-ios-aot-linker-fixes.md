# iOS AOT Build Fixes - 2025-01-01

## Summary
Fixed several issues blocking the iOS AOT simulator build after merging origin/main.

## Issues Fixed

### 1. Duplicate iOS Bundle Regeneration
**Problem**: The Makefile was calling ios-bundle twice - once for simulator via `build_ios_jank_aot.sh` and once for device via `ios-project` dependency.

**Fix**: Modified `ios-aot-sim` and `ios-aot-device` targets to:
- Depend directly on `build-shaders` instead of `ios-project`
- Call `generate-project.sh` directly instead of through `$(MAKE) ios-project`

**Files changed**: `/Users/pfeodrippe/dev/something/Makefile`

### 2. Missing C API Functions for WASM/iOS AOT
**Problem**: Linker errors for missing symbols:
- `jank_ns_intern_c`
- `jank_unbox_with_source`

**Fix**: Added these functions to `c_api_wasm.cpp`:
```cpp
jank_object_ref jank_ns_intern(jank_object_ref const sym);
jank_object_ref jank_ns_intern_c(char const * const sym);
void *jank_unbox_with_source(char const * const type, jank_object_ref const o, jank_object_ref const source);
```

**Files changed**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/c_api_wasm.cpp`

### 3. Missing error::warn Function
**Problem**: Linker error for `jank::error::warn(jtl::immutable_string const&)` - used by ns.cpp but not available in iOS AOT builds.

**Fix**:
1. Added `warn` function to `report_ios.cpp`:
```cpp
void warn(jtl::immutable_string const &msg)
{
  std::cerr << "\033[0;33mwarning:\033[0m " << msg << std::endl;
}
```
2. Added `report_ios.cpp` to iOS AOT build sources in CMakeLists.txt

**Files changed**:
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/error/report_ios.cpp`
- `/Users/pfeodrippe/dev/jank/compiler+runtime/CMakeLists.txt`

## Build Commands Used
```bash
# Rebuild jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Clean and rebuild iOS simulator
rm -rf SdfViewerMobile/build-iphonesimulator
rm -rf /Users/pfeodrippe/dev/jank/compiler+runtime/build-ios-simulator
make ios-aot-sim-run
```

## Next Steps
- Test JIT in the iOS simulator (if applicable)
- The iOS AOT app should now run in the simulator showing the SDF viewer
