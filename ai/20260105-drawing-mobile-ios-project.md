# DrawingMobile iOS Project Creation

## Date: 2026-01-05

## Summary

Created DrawingMobile iOS project for iPad, structured similarly to SdfViewerMobile but simplified (no Vulkan/MoltenVK - uses SDL3 with Metal backend directly).

## Files Created

### DrawingMobile/ directory:
- `config-common.yml` - Shared XcodeGen config (simplified, no Vulkan)
- `project-jit-sim.yml` - JIT Simulator project spec
- `project-jit-device.yml` - JIT Device project spec
- `main.mm` - iOS entry point (SDL3 init)
- `drawing_mobile_ios.mm` - C++ bridge to jank, loads `vybe.app.drawing` namespace
- `Info.plist` - iOS app configuration
- `LaunchScreen.storyboard` - Launch screen UI
- `jank-jit.entitlements` - JIT entitlements (empty plist)
- `generate-project.sh` - XcodeGen wrapper
- `setup_ios_deps.sh` - Downloads/builds SDL3 for iOS (no MoltenVK needed)
- `build_ios_jank_jit.sh` - Builds JIT bundle
- `build-ios-pch.sh` - Builds precompiled header
- `README.md` - Documentation

## Makefile Targets Added

```makefile
# Setup
make drawing-ios-setup              # Download SDL3 for iOS

# Simulator (port 5572 for compile server)
make drawing-ios-jit-sim-run        # Build and run (auto-starts server)
make drawing-ios-compile-server-sim # Standalone compile server

# Device (port 5573 for compile server)
make drawing-ios-jit-device-run     # Build and run (auto-starts server)
make drawing-ios-compile-server-device
make drawing-ios-device-nrepl-proxy # Mac:5581 -> Device:5580
```

## CI/CD

Added `build-drawing-ios-jit` job to `.github/workflows/ci.yml`:
- Caches SDL3.xcframework separately (`drawing-ios-frameworks-v1`)
- Reuses iOS LLVM cache from SdfViewerMobile
- Builds DrawingMobile JIT simulator app

## Key Differences from SdfViewerMobile

| Aspect | SdfViewerMobile | DrawingMobile |
|--------|----------------|---------------|
| Renderer | Vulkan (MoltenVK) | SDL3 Metal backend |
| Frameworks | MoltenVK + SDL3 | SDL3 only |
| Shaders | SPIR-V compilation | None (2D triangles) |
| Compile server port | 5570/5571 | 5572/5573 |
| nREPL proxy port | 5559 | 5581 |

## Cross-Platform Code

The same `vybe.app.drawing` namespace works on both macOS and iOS - no separate iOS namespace needed because SDL3 is cross-platform.

## Port Mapping

| App | Mode | Compile Server | nREPL |
|-----|------|---------------|-------|
| SdfViewer | Simulator | 5570 | 5558 |
| SdfViewer | Device | 5571 | 5559 (via iproxy) |
| Drawing | Simulator | 5572 | 5580 |
| Drawing | Device | 5573 | 5581 (via iproxy) |

## Next Steps

1. Run `make drawing-ios-setup` to download SDL3
2. Run `make ios-jit-llvm-sim` if not already done (~2 hours)
3. Run `make drawing-ios-jit-sim-run` to build and launch

## Commands Used

```bash
mkdir -p DrawingMobile
chmod +x DrawingMobile/*.sh
# Created all config files via Write tool
# Added Makefile targets
# Added CI/CD job to ci.yml
```
