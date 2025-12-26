# iOS JIT CI Build Fix

## Summary
Fixed the iOS JIT simulator build in CI which was failing because the JIT libraries weren't being built.

## Problem
The `build-ios-jit` CI job was failing with:
```
ERROR: Simulator JIT libraries not found!
make: *** [ios-jit-sim-libs] Error 1
```

The iOS JIT build requires:
1. **iOS LLVM** - A cross-compiled LLVM for iOS simulator (~541MB installed, ~2 hours to build)
2. **jank iOS JIT libs** - libjank.a and dependencies compiled with JIT support for iOS

## Solution

### 1. Added LLVM source to cache
The `build-ios-llvm` script needs the LLVM source at `build/llvm`. Updated the Clang/LLVM cache to include:
- `~/jank/compiler+runtime/build/llvm-install` (existing)
- `~/jank/compiler+runtime/build/llvm` (added)

Bumped cache key from `v1` to `v2`.

### 2. Added iOS LLVM cache and build step
Added a cache for the cross-compiled iOS LLVM:
- Path: `~/dev/ios-llvm-build/ios-llvm-simulator`
- Key: `ios-llvm-simulator-${{ env.JANK_REF }}-v1`

If not cached, builds iOS LLVM using:
```bash
cd ~/jank/compiler+runtime
./bin/build-ios-llvm simulator
```

### 3. Added jank iOS JIT libs build step
Before running the Makefile target, build jank with JIT for iOS:
```bash
cd ~/jank/compiler+runtime
./bin/build-ios build-ios-sim-jit Debug simulator jit
```

### 4. Increased timeout
Changed from 60 to 180 minutes to accommodate the iOS LLVM build (first run only).

## Key Files Modified
- `.github/workflows/ci.yml`

## CI Flow for iOS JIT
1. Restore Clang/LLVM cache (includes LLVM source)
2. Restore jank build cache
3. Restore iOS LLVM cache (if exists)
4. Build iOS LLVM if not cached (~2 hours first time)
5. Build jank iOS JIT libs (~5-10 minutes)
6. Run `make ios-jit-sim-build` to build the iOS app

## Commands Used
```bash
# View iOS LLVM size
du -sh ~/dev/ios-llvm-build/ios-llvm-simulator  # ~541MB

# Build iOS LLVM manually
cd ~/jank/compiler+runtime
./bin/build-ios-llvm simulator

# Build jank iOS JIT libs manually
cd ~/jank/compiler+runtime
./bin/build-ios build-ios-sim-jit Debug simulator jit
```

## Issue: ABI Mismatch After Cache Key Bump

When bumping the Clang/LLVM cache key (v1 -> v2), we also need to bump the jank build cache key (v2 -> v3). Otherwise:
- New LLVM is built/cached
- Old jank binary (from v2 cache) tries to load new LLVM
- Symbol mismatch error: `Symbol not found: __ZNK5clang13SourceManager21getSpellingLineNumberENS_14SourceLocationEPb`

**Rule:** Always bump jank build cache version when bumping LLVM cache version.

## Issue: Missing exported_scene.glb in CI

The `xcodegen` spec referenced `exported_scene.glb` which is:
1. A symlink: `SdfViewerMobile/exported_scene.glb -> ../exported_scene.glb`
2. Gitignored: `*.glb` in `.gitignore`

**Fix:** Made the resource optional in `SdfViewerMobile/project.yml`:
```yaml
- path: exported_scene.glb
  buildPhase: resources
  optional: true
```

## Issue: CppInterOp Doesn't Support LLVM 22.x

CppInterOp in jank's repo only supports LLVM up to 19.1.x, but jank uses LLVM 22.0.0git.

Error in CI:
```
CMake Error at CMakeLists.txt:151 (message):
  Found unsupported version: LLVM 22.0.0git;
  Please set LLVM_DIR pointing to the llvm version 13.0 to 19.1.x
```

**Fix:** Added sed commands in CI to patch CppInterOp before building:
```bash
sed -i '' 's/LLVM_MAX_SUPPORTED "19.1.x"/LLVM_MAX_SUPPORTED "22.x"/' third-party/cppinterop/CMakeLists.txt
sed -i '' 's/LLVM_VERSION_UPPER_BOUND 20.0.0/LLVM_VERSION_UPPER_BOUND 23.0.0/' third-party/cppinterop/CMakeLists.txt
# (same for CLANG and LLD)
```

## Issue: Missing iOS Frameworks (MoltenVK, SDL3)

The iOS build requires MoltenVK.xcframework and SDL3.xcframework, which are gitignored.

**Fix:** Added steps to run `setup_ios_deps.sh` which:
1. Downloads MoltenVK from GitHub releases
2. Builds SDL3 from source (~10 min)

Also added cache for `SdfViewerMobile/Frameworks` with key `ios-frameworks-v1`.

## Issue: exported_scene.glb Still Fails During Xcode Build

Even with `optional: true` in xcodegen spec, Xcode still tries to copy the file during build.

**Fix:** Added step in CI to create placeholder:
```bash
touch SdfViewerMobile/exported_scene.glb
```

## Issue: Missing vk_video Headers

Error in CI:
```
fatal error: 'vk_video/vulkan_video_codec_h264std.h' file not found
```

The `vk_video` directory contains Vulkan video codec headers (H.264, H.265, AV1, etc.) which are part of the MoltenVK release but weren't being copied by `setup_ios_deps.sh`.

**Why it worked locally but not in CI:**
- Locally, the `Frameworks/include/vk_video/` directory already existed from a previous setup (possibly from an older MoltenVK version or manual copy)
- In CI, the `ios-frameworks-v1` cache was created when `setup_ios_deps.sh` wasn't copying vk_video
- Subsequent CI runs used the cached Frameworks without vk_video headers
- This is a common pattern: local environment has state that was never captured in CI

**Fix:** Updated `setup_ios_deps.sh` to also copy vk_video headers:
```bash
if [ -d "$TEMP_DIR/MoltenVK/MoltenVK/include/vk_video" ]; then
    cp -R "$TEMP_DIR/MoltenVK/MoltenVK/include/vk_video" "$FRAMEWORKS_DIR/include/"
fi
```

Bumped iOS frameworks cache key from `v1` to `v2` to force rebuild.

## Next Steps
- Push changes and verify CI passes
- First run will take ~2 hours to build iOS LLVM, but subsequent runs will use cache
- Consider caching the jank iOS JIT build output to speed up future runs
