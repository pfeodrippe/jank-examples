# iOS Project Files Consolidation

## Summary
Consolidated iOS XcodeGen project files from 7 files down to 4 with consistent naming.

## New Structure (4 files)

| File | Purpose | Target Name | Build Command |
|------|---------|-------------|---------------|
| `project-aot-sim.yml` | AOT Simulator | SdfViewerMobile-AOT-Sim | `make ios-aot-sim-run` |
| `project-aot-device.yml` | AOT Device | SdfViewerMobile-AOT-Device | `make ios-aot-device-run` |
| `project-jit-sim.yml` | JIT Simulator (compile server) | SdfViewerMobile-JIT-Sim | `make ios-jit-sim-run` |
| `project-jit-device.yml` | JIT Device (compile server) | SdfViewerMobile-JIT-Device | `make ios-jit-device-run` |

## Files Removed
- `project.yml` (replaced by project-aot-sim.yml)
- `project-jit.yml` (generic JIT, not needed)
- `project-jit-sim.yml` (old, bundled vybe_aot)
- `project-jit-device.yml` (old, bundled vybe_aot)
- `project-jit-only-sim.yml` (became project-jit-sim.yml)
- `project-jit-only-device.yml` (became project-jit-device.yml)

## Key Changes

### Naming Convention
- AOT targets: `SdfViewerMobile-AOT-{Sim,Device}`
- JIT targets: `SdfViewerMobile-JIT-{Sim,Device}`

### JIT Mode Change
The "JIT-Only" mode (with compile server) is now the standard JIT mode. The old JIT mode that bundled `vybe_aot` library was removed.

JIT mode now:
- Bundles only core libs (clojure.core, etc.)
- Loads app namespaces via remote compile server
- No redeploy needed for code changes

### Makefile Updates
- Removed `ios-jit-only-*` targets (renamed to `ios-jit-*`)
- Removed old JIT targets that bundled vybe_aot
- Updated help text to reflect new structure
- Added `ios-aot-device-project` target

### Bundle IDs
- AOT Sim: `com.vybe.SdfViewerMobile-AOT-Sim`
- AOT Device: `com.vybe.SdfViewerMobile-AOT-Device`
- JIT Sim: `com.vybe.SdfViewerMobile-JIT-Sim`
- JIT Device: `com.vybe.SdfViewerMobile-JIT-Device`

## Commands

### AOT Builds
```bash
make ios-aot-sim-run       # Simulator
make ios-aot-device-run    # Device
```

### JIT Builds (auto-starts compile server)
```bash
make ios-jit-sim-run       # Simulator (port 5570)
make ios-jit-device-run    # Device (port 5571)
```

## Files Modified
- `SdfViewerMobile/generate-project.sh` - default changed to `project-aot-sim.yml`
- `Makefile` - extensive updates to targets and help text
- Created: `project-aot-sim.yml`, `project-aot-device.yml`, `project-jit-sim.yml`, `project-jit-device.yml`
- Deleted: old project files

## Notes
- The `config-common.yml` remains unchanged (shared configuration)
- JIT projects still cannot use `CommonHeaders` group due to LLVM header path needs
- XcodeGen creates project files named after the project `name` field:
  - `SdfViewerMobile-AOT-Sim.xcodeproj`
  - `SdfViewerMobile-AOT-Device.xcodeproj`
  - `SdfViewerMobile-JIT-Sim.xcodeproj`
  - `SdfViewerMobile-JIT-Device.xcodeproj`

## Verified Working
- ✅ `make ios-aot-sim-run` - AOT Simulator
- ✅ `make ios-jit-sim-run` - JIT Simulator (with compile server)
- ✅ `make ios-jit-device-run` - JIT Device (build succeeded, install blocked by free profile app limit)
- ⏳ `make ios-aot-device-run` - AOT Device (needs provisioning setup for new bundle ID)

## CI Updated
- `.github/workflows/ci.yml` - changed `sdf-ios-sim-build` to `ios-aot-sim-build`

## Free Developer Profile Note
Free developer profiles have a limit of 3 apps installed at once. If install fails with "maximum number of installed apps" error, delete an existing app from the device first.

## CI Fix (2025-12-30)
Fixed XcodeGen validation error in CI for JIT builds:
- Error: `Target "SdfViewerMobile-JIT-Sim" has a missing source directory "jank-resources/.clang-format"`
- Cause: `ios-jit-sim-project` ran before `ios-jit-sync-sources`, so `jank-resources/.clang-format` didn't exist
- Fix:
  1. Added `ios-jit-sync-sources` as dependency of `ios-jit-sim-project` and `ios-jit-device-project`
  2. Added `.clang-format` copy to `ios-jit-sync-sources` (from `JANK_SRC/build-ios-sim-jit/`)
