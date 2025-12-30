# iOS Simulator Targets Verification

## Summary
All three iOS simulator targets have been verified to build and run correctly after fixing the XcodeGen header path issue.

## Targets Verified
1. **iOS AOT Simulator** - `make sdf-ios-sim-build` / `make sdf-ios-sim-run`
2. **iOS JIT Simulator** - `make ios-jit-sim-build` / `make ios-jit-sim-run`
3. **iOS JIT-Only Simulator** - `make ios-jit-only-sim-build` / `make ios-jit-only-sim-run`

## Key Fix: XcodeGen Header Paths for JIT Projects

### Problem
JIT projects need LLVM/CppInterop headers in addition to the common headers. When using:
```yaml
settings:
  groups: [CommonHeaders, CommonCodeSign]
  base:
    HEADER_SEARCH_PATHS:
      - ${HOME}/dev/ios-llvm-build/ios-llvm-simulator/include  # Additional JIT header
```

The `HEADER_SEARCH_PATHS` in `base:` **REPLACES** the paths from `CommonHeaders` rather than merging with them, causing `'imgui.h' file not found` errors.

### Solution
JIT projects must NOT use `CommonHeaders` group. Instead, specify all headers directly:
```yaml
settings:
  groups: [CommonCodeSign]  # NO CommonHeaders!
  base:
    HEADER_SEARCH_PATHS:
      # All common headers
      - $(PROJECT_DIR)/../vendor/imgui
      - $(PROJECT_DIR)/../vendor/imgui/backends
      - $(PROJECT_DIR)/../vendor/flecs/distr
      # ... all other common headers ...
      # JIT-specific headers
      - ${HOME}/dev/ios-llvm-build/ios-llvm-simulator/include
      - ${JANK_SRC}/third-party/cppinterop/include
```

## Files Modified
- `SdfViewerMobile/project-jit.yml`
- `SdfViewerMobile/project-jit-device.yml`
- `SdfViewerMobile/project-jit-only-sim.yml`
- `SdfViewerMobile/project-jit-only-device.yml`
- `ai/20251229-xcodegen-shared-config.md` (added caveat documentation)

## Commands Used
```bash
# Build and run AOT Simulator
make sdf-ios-sim-build
make sdf-ios-sim-run

# Build and run JIT Simulator
make ios-jit-sim-build
make ios-jit-sim-run

# Build and run JIT-Only Simulator (starts compile server automatically)
make ios-jit-only-sim-build
make ios-jit-only-sim-run
```

## Important: JIT-Only Mode
The JIT-Only simulator requires the compile server running on macOS. The `make ios-jit-only-sim-run` command handles this automatically by:
1. Starting the compile server on port 5570
2. Building and installing the app
3. Launching the simulator

The app connects to the compile server and compiles all modules remotely (vybe.math, vybe.sdf, etc.).

## Next Steps
- Device builds should also be tested on physical hardware
- The device JIT builds have been updated with the same header path fix
