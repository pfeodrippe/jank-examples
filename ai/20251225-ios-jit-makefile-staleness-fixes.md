# iOS JIT Makefile Staleness & Build Fixes - December 25, 2025

## Summary

Fixed iOS JIT builds to properly handle staleness detection and ensure all build artifacts are correctly copied between AOT and JIT build directories.

## Issues Fixed

### 1. jank_aot_init.cpp Not Copied to JIT Build Directory
**Error**: `Unable to find module 'jank.nrepl-server.asio'`

**Root Cause**: The simulator JIT build wasn't copying `jank_aot_init.cpp` from the AOT build directory. This file contains the `#ifdef JANK_IOS_JIT` guards that enable nREPL module loading.

**Fix in Makefile** (`ios-jit-sim-libs` target):
```makefile
@# Copy jank_aot_init.cpp from AOT build
@if [ -f "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ]; then \
    cp SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp SdfViewerMobile/build-iphonesimulator-jit/generated/; \
fi
```

### 2. ios-bundle Script Changes Not Detected
**Problem**: Makefile didn't rebuild when `ios-bundle` script changed, causing stale `jank_aot_init.cpp` to be used.

**Fix in Makefile** (`ios-jit-sim-aot` and `ios-jit-device-aot` targets):
```makefile
@if [ ... ] || \
   [ "$(JANK_SRC)/bin/ios-bundle" -nt "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ]; then \
    ./SdfViewerMobile/build_ios_jank_aot.sh simulator; \
fi
```

### 3. rsync Target Directories Missing
**Error**: rsync failed in CI because target directories didn't exist.

**Fix**: Added `mkdir -p` before all rsync commands in `ios-jit-sync-sources` and `ios-jit-sync-includes` targets.

### 4. Shell Scripts Gitignored
**Error**: `build-ios-pch.sh: No such file or directory` in CI

**Root Cause**: Pattern `SDFViewerMobile/build-*` was ignoring shell scripts.

**Fix in .gitignore**:
```
SDFViewerMobile/build-*
!SDFViewerMobile/*.sh
```

## Key Files Modified

- **Makefile**: Added staleness detection for ios-bundle, copy step for jank_aot_init.cpp, mkdir for rsync
- **.gitignore**: Added negative pattern to preserve .sh files
- **project-jit-sim.yml** / **project-jit-device.yml**: XcodeGen specs (reviewed, no changes needed)

## Commands Used

```bash
# Run iOS simulator JIT build
make ios-jit-sim-run

# Run iOS device JIT build
make ios-jit-device-run
```

## Key Learnings

1. **Staleness detection must include build scripts**: When a build script generates code, the Makefile should check if the script itself is newer than the generated output.

2. **Copy generated files between build configurations**: JIT builds need files from AOT builds - ensure the copy step is in the dependency chain.

3. **rsync requires target directories**: Always `mkdir -p` before rsync in Makefiles.

4. **Gitignore negative patterns**: Use `!pattern` to un-ignore files matched by earlier patterns.

5. **JANK_IOS_JIT preprocessor define**: This controls whether nREPL modules are loaded in `jank_aot_init.cpp`. The file must be compiled by Xcode (not from libvybe_aot.a) to get this define.

## Build Flow

```
ios-jit-sim-run
  └── ios-jit-sim-xcode
        └── ios-jit-sim-libs
              ├── ios-jit-sim-aot (generates jank_aot_init.cpp)
              ├── copies jank_aot_init.cpp to JIT build dir
              └── copies other libs (libjank.a, libllvm_merged.a, etc.)
```

## Verification

Successfully ran `make ios-jit-sim-run` - build completed and app launched in simulator with nREPL working.
