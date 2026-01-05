# DrawingMobile

iOS/iPadOS version of the Looom-inspired drawing canvas app.

## Prerequisites

- Xcode 15+
- XcodeGen (`brew install xcodegen`)
- jank compiler built (`make build-jank-macos` from root)
- iOS LLVM for JIT mode (`make ios-jit-llvm-sim`)

## Quick Start (JIT Mode - Development)

JIT mode allows hot-reloading code from macOS. Best for development.

```bash
# One-time setup (downloads SDL3, ~10 min)
make drawing-ios-setup

# One-time: Build iOS LLVM (~2 hours)
make ios-jit-llvm-sim

# Build and run (auto-starts compile server)
make drawing-ios-jit-sim-run
```

Then connect nREPL to `localhost:5580` for REPL access.

## Build Modes

### JIT Mode (Development)
- App loads code from compile server on macOS
- Hot-reload: edit .jank files, re-require from REPL
- nREPL on port 5580

**Simulator:**
```bash
make drawing-ios-jit-sim-run        # Auto-starts compile server
make drawing-ios-compile-server-sim # Standalone compile server
```

**Device:**
```bash
make drawing-ios-jit-device-run           # Auto-starts server + iproxy
make drawing-ios-compile-server-device    # Standalone compile server
make drawing-ios-device-nrepl-proxy       # Forward nREPL (Mac:5581 -> Device:5580)
```

### AOT Mode (Release)
- All code compiled ahead-of-time
- No compile server needed
- Faster startup, App Store compatible

```bash
make drawing-ios-aot-sim-run    # Simulator
make drawing-ios-aot-device-run # Device
```

## Project Structure

```
DrawingMobile/
├── config-common.yml       # Shared XcodeGen config
├── project-jit-sim.yml     # JIT Simulator project
├── project-jit-device.yml  # JIT Device project
├── main.mm                 # iOS entry point
├── drawing_mobile_ios.mm   # C++ bridge to jank
├── Info.plist              # iOS app config
├── LaunchScreen.storyboard # Launch screen
├── jank-jit.entitlements   # JIT entitlements
├── setup_ios_deps.sh       # Download SDL3
├── build_ios_jank_jit.sh   # Build JIT bundle
├── build-ios-pch.sh        # Build precompiled header
└── generate-project.sh     # XcodeGen wrapper
```

## Differences from SdfViewerMobile

- **No Vulkan/MoltenVK**: Uses SDL3 with Metal backend directly
- **Simpler rendering**: 2D triangles only, no shaders to compile
- **Touch-first**: Optimized for Apple Pencil input

## Ports

| Mode | Compile Server | nREPL |
|------|---------------|-------|
| Simulator JIT | 5570 | 5580 |
| Device JIT | 5571 | 5580 (via iproxy 5581) |
