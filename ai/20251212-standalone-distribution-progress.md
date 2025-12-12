# Standalone Distribution Progress

## What Works
- `./bin/run_sdf.sh --standalone` - Creates executable
- `./bin/run_sdf.sh --standalone --static` - Creates executable with static SDL3/shaderc/MoltenVK
- macOS .app bundle creation with codesigning

## Current Issues

### 1. MoltenVK Static Linking Issue
When MoltenVK is linked statically, the Vulkan loader can't find an ICD (Installable Client Driver).
The error: "Vulkan instance creation failed"

**Why**: The Vulkan loader (`libvulkan.dylib`) discovers ICDs via JSON manifests. With MoltenVK linked statically, there's no separate dylib for the loader to find.

**Solutions**:
1. Bundle MoltenVK.dylib dynamically instead of static
2. OR modify sdf_engine to bypass vulkan loader when MoltenVK is static
3. OR set VK_ICD_FILENAMES environment variable

### 2. libsdf_deps.dylib Dependencies
The shared library created for JIT symbol resolution links against homebrew SDL3/vulkan/shaderc. Even with static linking in the final binary, these dylibs get loaded at runtime.

**Fix applied**: For `--static` builds, libsdf_deps.dylib is now created without linking SDL3/vulkan/shaderc (using `-Wl,-undefined,dynamic_lookup`)

### 3. Duplicate ObjC Classes
When homebrew SDL3 dylib is loaded alongside static SDL3 in the binary, ObjC runtime complains about duplicate class implementations.

## Build Script Usage

```bash
# JIT mode (development)
./bin/run_sdf.sh

# Standalone (dynamic libs)
./bin/run_sdf.sh --standalone

# Standalone with static libs (more portable, but MoltenVK issue)
./bin/run_sdf.sh --standalone --static

# Custom output name
./bin/run_sdf.sh --standalone -o MyApp
```

## App Bundle Structure (macOS)
```
SDFViewer.app/
  Contents/
    MacOS/SDFViewer (executable)
    Frameworks/
      libLLVM.dylib (jank runtime)
      libclang-cpp.dylib
      libc++.1.dylib
      libunwind.1.dylib
      libsdf_deps.dylib (our imgui code)
      libcrypto.3.dylib (jank dep)
      libzstd.1.dylib (jank dep)
    Resources/
      vulkan_kim/ (shaders)
    Info.plist
```

## Remaining Work
1. Fix MoltenVK/Vulkan initialization for static builds
2. OR switch to bundling dynamic MoltenVK with proper ICD setup
3. Test on clean Mac without homebrew to verify portability
