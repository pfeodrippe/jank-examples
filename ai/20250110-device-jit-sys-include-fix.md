# Device JIT Fixes (sys_include + Stack Overflow)

**Status: WORKING** ✓

## Problem
Device JIT was crashing with:
```
/private/var/.../include/c++/v1/stdlib.h:145:30: error: unknown type name 'ldiv_t'
```

## Root Cause
The `sys_include` directory (iOS SDK C headers) was missing from DrawingMobile's `jank-resources/include/`.

The jank JIT expects these include paths (from `environment_ios.cpp`):
- `/include/c++/v1` - libc++ headers
- `/clang/include` - clang builtin headers
- `/include/sys_include` - **iOS SDK C headers** (stdlib.h with ldiv_t, lldiv_t, etc.)

DrawingMobile had the first two but was missing `sys_include`.

## Fix

### 1. Copy iOS SDK headers
```bash
IOS_SDK=$(xcrun --sdk iphoneos --show-sdk-path)
cp -r "$IOS_SDK/usr/include" DrawingMobile/jank-resources/include/sys_include
```

### 2. Updated setup_ios_deps.sh
Added section to automatically copy from iOS SDK:
```bash
IOS_SDK_PATH=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
if [ -d "$IOS_SDK_PATH/usr/include" ]; then
    cp -r "$IOS_SDK_PATH/usr/include" jank-resources/include/sys_include
fi
```

## Files Modified
- `/Users/pfeodrippe/dev/something/DrawingMobile/setup_ios_deps.sh`:
  - Added sys_include copying from iOS SDK
  - Removed SDL3 copying from SdfViewerMobile (now builds independently)
- Created `DrawingMobile/jank-resources/include/sys_include/` (235 files from iOS SDK)

## Additional Fix: Stack Overflow on Device

After fixing sys_include, got stack overflow:
```
#0 ___chkstk_darwin
...
core::require_113295::call ()
call_jank_metal_main at drawing_mobile_ios.mm:265
```

**Cause:** `init_jank_runtime_on_large_stack()` runs on 8MB thread but returns. Then `call_jank_metal_main()` runs on main thread (1MB) which triggers `require` → deep JIT recursion → stack overflow.

**Fix:** Move `call_jank_metal_main()` inside the large-stack thread function:
```cpp
static void* jank_init_thread_func(void* arg) {
    bool success = init_jank_runtime_impl();

    // Also call jank_metal_main on this large-stack thread!
    if (success) {
        call_jank_metal_main();  // <-- Now runs on 8MB stack
    }
    ...
}
```

## Key Learnings
1. Each iOS app needs its own `sys_include` copied from the iOS SDK
2. JIT `require` calls need 8MB stack - must run on large-stack thread, not main thread
3. Don't share dependencies between iOS projects - copy from SDK directly
