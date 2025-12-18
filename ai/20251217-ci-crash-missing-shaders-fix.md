# CI App Crash Fix: Missing Compute Shaders

**Date**: 2025-12-17
**Issue**: EXC_BAD_ACCESS (SIGBUS) crash in CI-built macOS app, works locally

## Problem Summary

The CI-built SDFViewer app crashes with `KERN_PROTECTION_FAILURE at 0x6e00480000` (GPU reserved memory region) when calling `sdfx::process_dc_chunk()`.

### Crash Stack
```
0   sdfx::process_dc_chunk + 724
1   ??? 0x801201ff  (corrupted return address)
2   sdfx::generate_mesh_dc_gpu_chunked + 788
3   sdfx::generate_mesh_preview + 1036
```

## Root Causes Found

### 1. Missing Compute Shaders in CI Build (PRIMARY CAUSE)

The Makefile only compiled 4 shaders:
```makefile
SHADERS_SPV = vulkan_kim/blit.vert.spv vulkan_kim/blit.frag.spv \
              vulkan_kim/mesh.vert.spv vulkan_kim/mesh.frag.spv
```

Missing critical DC compute shaders:
- `dc_mark_active.spv`
- `dc_vertices.spv`
- `dc_quads.spv`
- `dc_cubes.spv`
- `sdf_sampler.spv`
- `sdf_scene.spv`

CI app: 4 shaders, Local app: 10 shaders

### 2. `find_memory_type` Returns 0 on Failure

```cpp
inline uint32_t find_memory_type(...) {
    // ...
    return 0;  // BUG: Returns memory type 0 if no suitable type found
}
```

Memory type 0 on some GPUs is device-local only (cannot be mapped from CPU).

### 3. No Error Checking for `vkMapMemory`

```cpp
vkMapMemory(e->device, dc->vertexMemory, 0, size, 0, &data);
float* verts = (float*)data;  // data is GARBAGE if vkMapMemory failed!
```

If `vkMapMemory` fails (e.g., on non-host-visible memory), `data` contains garbage pointing to GPU reserved address space.

## Why It Worked Locally But Not on CI

1. CI runner (`macos-14`) has different GPU memory type layout
2. When `find_memory_type` couldn't find HOST_VISIBLE memory, it returned 0 (device-local)
3. Memory allocated as GPU-only
4. `vkMapMemory` failed (silently, no error check)
5. Code proceeded to dereference garbage pointer `0x6e00480000` (GPU Carveout region)
6. CRASH

## Fixes Applied

### 1. Updated Makefile (`Makefile`)
```makefile
# Basic rendering shaders
SHADERS_BASIC = vulkan_kim/blit.vert.spv vulkan_kim/blit.frag.spv \
                vulkan_kim/mesh.vert.spv vulkan_kim/mesh.frag.spv

# Compute shaders for SDF and mesh generation
SHADERS_COMPUTE = vulkan_kim/sdf_sampler.spv vulkan_kim/sdf_scene.spv \
                  vulkan_kim/dc_mark_active.spv vulkan_kim/dc_vertices.spv \
                  vulkan_kim/dc_quads.spv vulkan_kim/dc_cubes.spv

SHADERS_SPV = $(SHADERS_BASIC) $(SHADERS_COMPUTE)
```

Added proper pattern rules for `.comp` -> `.spv` compilation.

### 2. Fixed `find_memory_type` (`vulkan/sdf_engine.hpp:820`)
```cpp
// Returns UINT32_MAX if no suitable memory type found
inline uint32_t find_memory_type(...) {
    // ...
    std::cerr << "ERROR: No suitable memory type found..." << std::endl;
    return UINT32_MAX;
}
```

### 3. Added Memory Type Validation in Buffer Creation
```cpp
if (allocInfo.memoryTypeIndex == UINT32_MAX) {
    std::cerr << "ERROR: No host-visible memory type available" << std::endl;
    vkDestroyBuffer(e->device, buffer, nullptr);
    return false;
}
```

### 4. Added `vkMapMemory` Return Value Checks (`vulkan/sdf_engine.hpp:3949`)
```cpp
VkResult mapResult = vkMapMemory(...);
if (mapResult != VK_SUCCESS) {
    std::cerr << "ERROR: vkMapMemory failed: " << mapResult << std::endl;
    return mesh;
}
```

### 5. Added Sanity Check for Unreasonable Values
```cpp
const uint32_t MAX_REASONABLE_VERTICES = 10000000;
if (vertexCount > MAX_REASONABLE_VERTICES) {
    std::cerr << "ERROR: Unreasonable vertex count: " << vertexCount << std::endl;
    return mesh;
}
```

## Files Modified

1. `/Makefile` - Added compute shader compilation
2. `/vulkan/sdf_engine.hpp` - Fixed memory type detection and vkMapMemory error handling

## Commands Used

```bash
# Compare local vs CI apps
otool -L /path/to/SDFViewer.app/Contents/MacOS/SDFViewer-bin
ls -la /path/to/SDFViewer.app/Contents/Resources/vulkan_kim/*.spv
```

## Additional Fix: Linux Library Name

The Linux build script was looking for `libshaderc.so` but Ubuntu names it `libshaderc_shared.so`. This was causing the build to potentially hang or fail.

**Fixed in**: `bin/run_sdf.sh`
- Line 539: `libshaderc.so` -> `libshaderc_shared.so`
- Line 732: `libshaderc.so` -> `libshaderc_shared.so`

## Next Steps

1. Push these fixes and verify CI build includes all shaders
2. Test on user's machine with the fixed CI build
3. Consider adding Vulkan validation layers for debug builds
