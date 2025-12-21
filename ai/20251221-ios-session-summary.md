# iOS SDF Viewer Session Summary

**Date:** 2025-12-21

## Accomplishments

### 1. iOS App Running Successfully

The SdfViewerMobile iOS app is now fully functional on the iPad Pro 13-inch simulator:

- **Vulkan rendering via MoltenVK** - Same rendering stack as macOS
- **SDL3 for windowing/input** - Built as dylib for iOS
- **Pre-compiled SPIR-V shaders** - No runtime shader compilation needed
- **ImGui working** - Fixed crash by adding frame management functions

### 2. Key Fixes Applied

| Issue | Fix |
|-------|-----|
| VK_KHR_portability_enumeration not supported | Conditional compilation for iOS |
| shaderc not available | HAS_SHADERC conditional, use pre-compiled .spv |
| Shader source loading failed | iOS loads .spv directly, skips .comp |
| Resources not bundled | Added vulkan_kim to project.yml with buildPhase: resources |
| ImGui crash (SIGSEGV) | Added imgui_begin_frame() and imgui_end_frame() functions |

### 3. jank iOS AOT Research Complete

Key findings:

1. **wasm-aot generates platform-agnostic C++** - No jank compiler changes needed
2. **Challenge is building libjank.a for iOS** - Requires cross-compiling:
   - BDW-GC (Boehm garbage collector)
   - Boost libraries
   - Folly (Facebook's library) - most complex
3. **Implementation options documented** in `ai/20251221-ios-aot-implementation-plan.md`

## Files Created/Modified

### New Files

| File | Purpose |
|------|---------|
| `SdfViewerMobile/` | iOS app directory |
| `SdfViewerMobile/main.mm` | iOS entry point |
| `SdfViewerMobile/sdf_viewer_ios.mm` | Main app logic |
| `SdfViewerMobile/project.yml` | XcodeGen specification |
| `SdfViewerMobile/setup_ios_deps.sh` | Dependency setup script |
| `SdfViewerMobile/test_aot.jank` | Test jank file for AOT |
| `SdfViewerMobile/generated/test_aot.cpp` | Generated C++ from jank |
| `ai/20251221-ios-sdfviewer-implementation.md` | Implementation notes |
| `ai/20251221-ios-imgui-crash-fix.md` | ImGui crash fix documentation |
| `ai/20251221-ios-aot-implementation-plan.md` | Detailed iOS AOT plan |

### Modified Files

| File | Changes |
|------|---------|
| `vulkan/sdf_engine.hpp` | iOS compatibility (portability, shaderc, imgui frames) |

## Build Commands

```bash
# Setup dependencies
cd SdfViewerMobile && ./setup_ios_deps.sh

# Generate Xcode project
cd SdfViewerMobile && xcodegen generate

# Build for simulator
xcodebuild -project SdfViewerMobile.xcodeproj \
    -scheme SdfViewerMobile \
    -destination 'platform=iOS Simulator,id=57653CE6-DF09-4724-8B28-7CB6BA90E0E3' \
    -configuration Debug \
    CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO build

# Run on simulator
xcrun simctl install 57653CE6-DF09-4724-8B28-7CB6BA90E0E3 .../SdfViewerMobile.app
xcrun simctl launch --console 57653CE6-DF09-4724-8B28-7CB6BA90E0E3 com.vybe.SdfViewerMobile
```

## Next Steps

### Short Term (Current App)
- App is working! Can be used as-is for iOS development
- Touch gestures for camera control are available via SDL3

### Medium Term (jank iOS AOT)
1. Build BDW-GC for iOS
2. Build Boost for iOS
3. Evaluate Folly iOS compatibility
4. Create CMake toolchain for iOS
5. Build libjank_ios.a

### Alternative Approach
- Keep jank development on macOS
- Export SDF data as C++ structures
- Use native sdf_engine.hpp on iOS (already working!)

## Commands Learned

```bash
# Generate C++ from jank using wasm-aot
/Users/pfeodrippe/dev/jank/compiler+runtime/build/jank \
    --codegen wasm-aot \
    --save-cpp \
    --save-cpp-path ./output.cpp \
    run input.jank

# iPad simulator ID
57653CE6-DF09-4724-8B28-7CB6BA90E0E3
```
