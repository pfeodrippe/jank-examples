# iOS SDF Viewer Implementation

**Date:** 2025-12-21

## Summary

Successfully created an iOS (iPad) app that runs the SDF viewer using Vulkan via MoltenVK and SDL3. The app displays the Kim Kitsuragi SDF character using pre-compiled SPIR-V shaders.

## Key Technical Decisions

### 1. Vulkan via MoltenVK
- Used MoltenVK v1.2.9 as the Vulkan-to-Metal translation layer
- Same rendering stack as macOS version for maximum code reuse

### 2. SDL3 for Windowing/Input
- Built SDL3 for iOS simulator (arm64)
- SDL3 builds as a dylib, not a framework - required special xcframework creation

### 3. Pre-compiled Shaders (No Runtime Compilation)
- iOS cannot use shaderc for runtime shader compilation
- Solution: Use pre-compiled .spv files bundled with the app
- Added `HAS_SHADERC` conditional compilation to sdf_engine.hpp

## Files Created

### SdfViewerMobile/
- `main.mm` - iOS entry point with SDL3 initialization
- `sdf_viewer_ios.mm` - Main app logic using sdf_engine.hpp
- `project.yml` - XcodeGen project specification
- `Info.plist` - iOS app configuration
- `LaunchScreen.storyboard` - Launch screen UI
- `setup_ios_deps.sh` - Downloads MoltenVK, builds SDL3 for iOS
- `build_ios_jank.sh` - Placeholder for jank AOT compilation
- `.gitignore` - Ignores build artifacts and frameworks

## Key Fixes Applied

### 1. VK_KHR_portability_enumeration Not Supported on iOS

**Error:**
```
[mvk-error] VK_ERROR_EXTENSION_NOT_PRESENT: Vulkan extension VK_KHR_portability_enumeration is not supported.
```

**Fix in sdf_engine.hpp:**
```cpp
#if !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
    // VK_KHR_portability_enumeration is only needed on macOS, not iOS
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
```

### 2. Shaderc Not Available on iOS

**Error:**
```
'shaderc/shaderc.hpp' file not found
```

**Fix in sdf_engine.hpp:**
```cpp
#if !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
#include <shaderc/shaderc.hpp>
#define HAS_SHADERC 1
#else
#define HAS_SHADERC 0
// Dummy shaderc_shader_kind for iOS
#define shaderc_compute_shader 0
typedef int shaderc_shader_kind;
#endif
```

### 3. Shader Source Loading on iOS

**Error:**
```
Failed to open: .../hand_cigarette.comp
```

**Fix:** On iOS, skip reading .comp source and directly load .spv:
```cpp
#if HAS_SHADERC
    std::string glslSource = read_text_file(shaderPath);
    auto spirv = compile_glsl_to_spirv(glslSource, ...);
#else
    // iOS: load pre-compiled .spv directly
    auto spirv = compile_glsl_to_spirv("", shaderPath, shaderc_compute_shader);
#endif
```

### 4. Resources Not Bundled

**Error:**
```
Failed to open SPIR-V file: .../vulkan_kim/hand_cigarette.spv
```

**Fix:** In project.yml, add resources within sources section with buildPhase:
```yaml
sources:
  # ... source files ...
  - path: vulkan_kim
    buildPhase: resources
    type: folder
```

Also created symlink: `ln -sf ../vulkan_kim vulkan_kim`

### 5. Missing IOSurface Framework

**Error:** Undefined symbols from MoltenVK

**Fix:** Added to OTHER_LDFLAGS:
```yaml
- "-framework IOSurface"
- "-framework IOKit"
```

### 6. ImGui Crash (SIGSEGV at 0x28)

**Error:**
```
EXC_BAD_ACCESS (SIGSEGV) KERN_INVALID_ADDRESS at 0x0000000000000028
ImGui_ImplVulkan_RenderDrawData + 48
```

**Root Cause:** ImGui frame functions not called before `draw_frame()`:
- `ImGui_ImplVulkan_NewFrame()` - not called
- `ImGui_ImplSDL3_NewFrame()` - not called
- `ImGui::NewFrame()` - not called
- `ImGui::Render()` - not called

**Fix in sdf_engine.hpp:** Added helper functions:
```cpp
inline void imgui_begin_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

inline void imgui_end_frame() {
    ImGui::Render();
}
```

**Fix in sdf_viewer_ios.mm:** Call these in render loop:
```cpp
sdfx::imgui_begin_frame();
// UI code here
sdfx::imgui_end_frame();
sdfx::draw_frame();
```

## Build Commands

```bash
# Setup dependencies (download MoltenVK, build SDL3)
cd SdfViewerMobile && ./setup_ios_deps.sh

# Generate Xcode project
cd SdfViewerMobile && xcodegen generate

# Build for iPad simulator
xcodebuild -project SdfViewerMobile.xcodeproj \
  -scheme SdfViewerMobile \
  -destination 'platform=iOS Simulator,id=57653CE6-DF09-4724-8B28-7CB6BA90E0E3' \
  -configuration Debug \
  CODE_SIGN_IDENTITY="" CODE_SIGNING_REQUIRED=NO CODE_SIGNING_ALLOWED=NO \
  build

# Install and run on simulator
xcrun simctl install 57653CE6-DF09-4724-8B28-7CB6BA90E0E3 \
  .../SdfViewerMobile.app
xcrun simctl launch --console 57653CE6-DF09-4724-8B28-7CB6BA90E0E3 \
  com.vybe.SdfViewerMobile
```

## Next Steps

1. **Add jank iOS AOT support** - Similar to WASM approach:
   - Generate C++ code from jank
   - Compile with iOS clang for arm64
   - Link with the iOS app

2. **Test on real device** - Currently only tested on simulator

3. **Add touch gestures** - Orbit camera, pinch zoom, pan

## MCP Tool for iOS Simulator

For interacting with iOS simulator: https://github.com/joshuayoes/ios-simulator-mcp

## iPad Simulator Used

iPad Pro 13-inch (M4): `57653CE6-DF09-4724-8B28-7CB6BA90E0E3`
