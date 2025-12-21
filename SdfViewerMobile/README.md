# SDF Viewer Mobile - iOS

iOS version of the SDF Viewer for rendering Kim Kitsuragi from Disco Elysium using signed distance fields.

## Architecture

This app uses the same rendering stack as the macOS version:
- **SDL3** for windowing and input handling
- **Vulkan** via **MoltenVK** (translates Vulkan to Metal)
- **ImGui** for UI
- **Compute shaders** for SDF raymarching

## Prerequisites

1. **Xcode 15+** with iOS SDK
2. **xcodegen** for project generation: `brew install xcodegen`
3. **glslangValidator** for shader compilation: `brew install glslang`

## Building

### 1. Download/Build iOS Dependencies

The following frameworks are needed for iOS arm64:

```bash
# Run the setup script to download/build dependencies
./setup_ios_deps.sh
```

This will create a `Frameworks/` directory with:
- `SDL3.xcframework` - SDL3 built for iOS
- `MoltenVK.xcframework` - Vulkan to Metal translation layer

### 2. Generate Xcode Project

```bash
cd SdfViewerMobile
xcodegen generate
```

### 3. Open in Xcode

```bash
open SdfViewerMobile.xcodeproj
```

### 4. Build and Run

1. Select your iOS device or simulator
2. Build and run (Cmd+R)

## Project Structure

```
SdfViewerMobile/
├── Info.plist              # iOS app configuration
├── LaunchScreen.storyboard # Launch screen UI
├── main.mm                 # iOS entry point
├── sdf_viewer_ios.mm       # Main app logic
├── project.yml             # XcodeGen project spec
├── setup_ios_deps.sh       # Dependency setup script
└── Frameworks/             # iOS frameworks (generated)
    ├── SDL3.xcframework
    └── MoltenVK.xcframework
```

## Notes

### No JIT on iOS

iOS doesn't allow JIT compilation due to code signing restrictions. Therefore:
- The jank REPL functionality is disabled on iOS
- All jank code must be AOT-compiled before building
- Shader hot-reloading works (shaders are loaded at runtime, not JIT)

### Touch Controls

- **Single touch drag**: Orbit camera
- **Pinch**: Zoom in/out
- **Two-finger drag**: Pan camera
- **Tap**: Select object (in edit mode)

### Shader Compatibility

The GLSL compute shaders are compiled to SPIR-V, which MoltenVK translates to Metal Shading Language at runtime. The same `.spv` files work on both macOS and iOS.

## Troubleshooting

### "Metal is not supported"
The iOS Simulator doesn't fully support Metal compute shaders. Test on a real device.

### Shader compilation fails
Ensure glslangValidator is installed and the shaders compile on macOS first.

### Framework not found
Run `./setup_ios_deps.sh` to download and set up the required frameworks.
