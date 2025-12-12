# Standalone Distribution - Final Status

## Summary
Successfully implemented `--standalone` flag in `bin/run_sdf.sh` to generate fully portable macOS app bundles.

## What Works
- `./bin/run_sdf.sh --standalone -o SDFViewer` - Creates a portable .app bundle
- App bundle includes all necessary dynamic libraries in `Contents/Frameworks`
- Vulkan initialization works via bundled MoltenVK with proper ICD manifest
- No homebrew dependencies in the final binary (verified with otool -L)

## Bundle Structure
```
SDFViewer.app/
  Contents/
    MacOS/
      SDFViewer (launcher script - sets VK_ICD_FILENAMES env var)
      SDFViewer-bin (actual executable)
    Frameworks/
      libLLVM.dylib (jank runtime)
      libclang-cpp.dylib
      libc++.1.dylib
      libunwind.1.dylib
      libsdf_deps.dylib (our imgui code)
      libcrypto.3.dylib (jank dep)
      libzstd.1.dylib (jank dep)
      libSDL3.dylib
      libSDL3.0.dylib -> libSDL3.dylib (versioned symlink)
      libvulkan.dylib
      libvulkan.1.dylib -> libvulkan.dylib (versioned symlink)
      libshaderc_shared.dylib
      libshaderc_shared.1.dylib -> libshaderc_shared.dylib (versioned symlink)
      libMoltenVK.dylib
    Resources/
      vulkan_kim/ (shaders)
      vulkan/icd.d/MoltenVK_icd.json (ICD manifest for Vulkan loader)
    Info.plist
```

## Key Implementation Details

### 1. Versioned Library Symlinks
The jank AOT linker references versioned library names (libSDL3.0.dylib, libvulkan.1.dylib) but homebrew's versioned libraries are separate files. Solution: create symlinks in the Frameworks folder from versioned names to the actual libraries.

### 2. Vulkan ICD Setup
MoltenVK is Vulkan's "Installable Client Driver" on macOS. The Vulkan loader discovers it via JSON manifests. We:
1. Bundle `libMoltenVK.dylib` in Frameworks
2. Create `MoltenVK_icd.json` pointing to `../../../Frameworks/libMoltenVK.dylib`
3. Use a launcher script to set `VK_ICD_FILENAMES` environment variable

### 3. Library Path Fixing
All library paths are rewritten using `install_name_tool`:
- `@executable_path/../Frameworks/` for bundled libs
- `@rpath/` for jank runtime libs (with rpath set to Frameworks)

### 4. Code Signing
After modifying binaries with `install_name_tool`, macOS requires re-signing with `codesign --force --sign -`.

## Size
- App bundle: ~270MB (mainly due to LLVM/clang for jank runtime)

## Testing
Verified:
- No objc duplicate class warnings (no homebrew SDL3 conflict)
- Vulkan initializes correctly (GPU: Apple M3 Max)
- Shaders load and render correctly
- otool -L shows no homebrew paths

## Commands
```bash
# Build standalone app
./bin/run_sdf.sh --standalone -o SDFViewer

# Run
open SDFViewer.app
# or
SDFViewer.app/Contents/MacOS/SDFViewer

# Verify no external deps
otool -L SDFViewer.app/Contents/MacOS/SDFViewer-bin | grep homebrew
```

## Remaining Considerations
1. **Clean Mac testing**: Should test on a Mac without homebrew to verify full portability
2. **Size optimization**: The 270MB is mostly LLVM - if jank runtime could be stripped, size could reduce significantly
3. **`--static` option**: Still available but has MoltenVK/Vulkan loader architectural issues (documented in `20251212-standalone-distribution-progress.md`)
