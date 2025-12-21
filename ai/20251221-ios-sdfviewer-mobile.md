# iOS SDF Viewer Mobile - Session Notes

**Date:** 2025-12-21
**Task:** Create iOS app for running vybe.sdf with jank

## Summary

Created `SdfViewerMobile/` folder with a complete iOS app structure for running the SDF viewer on iPhone/iPad. **The app runs the same vybe.sdf jank code as macOS**, using AOT compilation to work around iOS's JIT restrictions.

## Architecture Decisions

### jank on iOS via AOT Compilation

iOS blocks JIT compilation due to code signing restrictions, but **jank supports AOT (Ahead-of-Time) compilation**. This means:

1. **Same vybe.sdf code** - The exact same jank code runs on iOS
2. **AOT compilation** - `jank compile --target arm64-apple-ios vybe.sdf` produces native iOS code
3. **Link into app** - The AOT-compiled modules are linked into the iOS app binary
4. **No REPL** - The nREPL server is disabled on iOS (no JIT means no runtime eval)

### Why MoltenVK + SDL3 (not native Metal)?

1. **Maximum code reuse** - The macOS version already uses Vulkan/MoltenVK
2. **Same GLSL shaders** - SPIR-V shaders work on both platforms
3. **SDL3 cross-platform** - Handles touch input, windowing, and lifecycle
4. **MoltenVK mature** - Vulkan to Metal translation layer is production-ready

### iOS-specific Considerations

- **AOT-only execution** - All jank code is pre-compiled to native arm64
- **No hot code reload** - Can't eval new code at runtime (iOS limitation)
- **Shader hot-reload works** - SPIR-V shaders are loaded at runtime (not JIT)
- **jank runtime for iOS** - The jank runtime library needs to be cross-compiled

## Files Created

```
SdfViewerMobile/
├── Info.plist              # iOS app configuration (arm64, Metal required)
├── LaunchScreen.storyboard # Launch screen with "SDF Viewer" branding
├── main.mm                 # iOS entry point (SDL3 initialization)
├── sdf_viewer_ios.mm       # jank AOT entry point (calls vybe.sdf/-main)
├── project.yml             # XcodeGen project specification
├── setup_ios_deps.sh       # Downloads MoltenVK, builds SDL3 for iOS
├── build_ios_jank.sh       # Cross-compiles dependencies for iOS arm64
└── README.md               # Build instructions
```

## Makefile Targets Added

```bash
make ios-setup    # Download/build iOS dependencies (MoltenVK, SDL3)
make ios-jank     # AOT compile jank code for iOS
make ios-project  # Generate Xcode project (requires xcodegen)
make ios-build    # Build iOS app with xcodebuild
make ios-clean    # Clean iOS artifacts
make ios          # Full build (setup + jank + project + build)
```

## Build Process

1. Install prerequisites: `brew install xcodegen glslang`
2. Setup dependencies: `make ios-setup`
3. Generate Xcode project: `make ios-project`
4. Open in Xcode: `open SdfViewerMobile/SdfViewerMobile.xcodeproj`
5. Build and run on device

## Key Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| MoltenVK   | 1.2.9   | Vulkan to Metal translation |
| SDL3       | 3.2.0   | Cross-platform windowing/input |
| xcodegen   | latest  | Project file generation |
| glslang    | latest  | GLSL to SPIR-V compilation |

## Touch Controls (iOS)

- **Single touch drag** → Orbit camera
- **Pinch gesture** → Zoom in/out
- **Two-finger drag** → Pan camera
- **Tap** → Select object (edit mode)

## What's Next (Priority Order)

1. **Add iOS target to jank compiler** - Enable `--target arm64-apple-ios` in jank
2. **Cross-compile jank runtime for iOS** - Build libjank_runtime for iOS arm64
3. **AOT compile vybe.sdf** - Generate iOS-compatible object files
4. **Test full build** - Run on actual iOS device
5. **Touch gesture refinement** - Improve pinch-to-zoom, pan gestures
6. **Mobile GPU optimization** - Profile and optimize shader for Apple GPU
7. **App Store assets** - Icons, screenshots, metadata

## jank iOS Integration Details

The iOS app calls into AOT-compiled jank code:

```
main.mm (SDL3 init)
    └── sdf_viewer_ios.mm
            ├── jank_runtime_init()      # Initialize jank runtime
            ├── vybe_DOT_sdf_SLASH__DASH_main()  # Call (vybe.sdf/-main)
            └── jank_runtime_shutdown()  # Cleanup
```

The entry point name `vybe_DOT_sdf_SLASH__DASH_main` follows jank's name mangling:
- `.` → `_DOT_`
- `/` → `_SLASH_`
- `-` → `_DASH_`

## Commands Used

```bash
# Created project structure
mkdir -p SdfViewerMobile

# Made setup scripts executable
chmod +x SdfViewerMobile/setup_ios_deps.sh
chmod +x SdfViewerMobile/build_ios_jank.sh

# Updated Makefile with iOS targets
```

## Technical Notes

### Project Configuration (project.yml)

- Uses XcodeGen for maintainable project files
- Targets iOS 15.0+ (Metal compute shader support)
- Links Metal, MetalKit, QuartzCore frameworks
- Bundles vulkan_kim shaders as resources
- Sets VK_USE_PLATFORM_IOS_MVK and VK_USE_PLATFORM_METAL_EXT

### Shader Compilation

GLSL compute shaders are compiled to SPIR-V on the host machine. MoltenVK translates SPIR-V to Metal Shading Language at runtime. Same `.spv` files work on both macOS and iOS.

### Bundle Structure

The iOS app bundles:
- Compiled SPIR-V shaders (vulkan_kim/*.spv)
- jank source files (for reference, not JIT)
- MoltenVK.xcframework
- SDL3.xcframework
