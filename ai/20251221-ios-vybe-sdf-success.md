# iOS vybe.sdf AOT Success

**Date:** 2025-12-21

## Summary

Successfully compiled and ran vybe.sdf on iPad using jank AOT compilation. The Kim Kitsuragi hand holding a cigarette renders perfectly using raymarched SDF graphics.

## Key Breakthrough: Weak Symbol Pattern

The main challenge was that jank's `cpp/raw` extern function declarations need to be JIT-compiled during AOT code generation. Since the actual C++ implementations exist only in the iOS project, we needed a way to provide stubs during AOT compilation.

**Solution:** Use `__attribute__((weak))` stub implementations in the `cpp/raw` block:

```clojure
(cpp/raw "
extern \"C\" {
  __attribute__((weak))
  bool vybe_ios_init_str(std::string const& shader_dir) { return true; }

  __attribute__((weak))
  void vybe_ios_cleanup() {}
  // ... more stubs
}
")
```

At iOS link time, the real implementations in `sdf_viewer_ios.mm` override the weak stubs.

## Files Created/Modified

### New Files
- `SdfViewerMobile/vybe_sdf_ios.jank` - iOS-specific SDF viewer using cpp/raw with weak stubs
- `SdfViewerMobile/generated/vybe_sdf_ios.cpp` - AOT-compiled C++ output

### Modified Files
- `SdfViewerMobile/sdf_viewer_ios.mm` - Added real C++ implementations for vybe_ios_* functions
- `SdfViewerMobile/project.yml` - Updated to use vybe_sdf_ios.cpp

## jank Syntax Learnings

1. **No docstrings in ns**: `(ns foo "docstring")` is INVALID
2. **Docstrings in defn are OK**: `(defn foo "docstring" [] ...)` works fine
3. **cpp/box takes only a pointer**: Don't use `(cpp/box :string cstr)`, just use the value directly
4. **std::string in extern "C"**: Can't return std::string from extern "C" functions - use const char* instead
5. **AOT loader naming**: `jank_load_<namespace_with_dots_as_underscores>` (e.g., `jank_load_vybe_sdf_ios`)

## Commands Used

```bash
# AOT compile jank module
/Users/pfeodrippe/dev/jank/compiler+runtime/build/jank \
  --codegen wasm-aot \
  --module-path . \
  --save-cpp \
  --save-cpp-path ./generated/vybe_sdf_ios.cpp \
  run vybe_sdf_ios.jank

# Build iOS app
xcodebuild -project SdfViewerMobile.xcodeproj \
  -scheme SdfViewerMobile \
  -sdk iphonesimulator \
  -destination "platform=iOS Simulator,name=iPad Pro 13-inch (M4)" \
  build

# Run on simulator
xcrun simctl install "iPad Pro 13-inch (M4)" .../SdfViewerMobile.app
xcrun simctl launch "iPad Pro 13-inch (M4)" com.vybe.SdfViewerMobile
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    iOS App Bundle                            │
├─────────────────────────────────────────────────────────────┤
│  sdf_viewer_ios.mm                                          │
│  ├── vybe_ios_* implementations (override weak stubs)       │
│  ├── init_jank_runtime()                                    │
│  └── call_jank_ios_main() → vybe.sdf-ios/ios-main          │
├─────────────────────────────────────────────────────────────┤
│  vybe_sdf_ios.cpp (AOT-compiled)                            │
│  ├── Weak stub implementations                              │
│  ├── jank function definitions                              │
│  └── jank_load_vybe_sdf_ios() entry point                   │
├─────────────────────────────────────────────────────────────┤
│  clojure_core_generated.cpp (AOT-compiled)                  │
│  └── clojure.core runtime                                   │
├─────────────────────────────────────────────────────────────┤
│  libjank.a, libgc.a, libjankzip.a                          │
│  └── jank runtime libraries for iOS                         │
├─────────────────────────────────────────────────────────────┤
│  sdf_engine.hpp (Vulkan/MoltenVK)                           │
│  └── SDF raymarching renderer                               │
└─────────────────────────────────────────────────────────────┘
```

## Result

The SDF viewer runs on iPad with:
- jank AOT runtime fully initialized
- vybe.sdf-ios module loaded
- Kim Kitsuragi hand rendered via raymarched SDFs
- Camera controllable via jank state

## Next Steps

1. Add touch controls for camera manipulation
2. Add ImGui UI elements via jank
3. Port more vybe.sdf functionality (shader switching, etc.)
4. Test on physical device
