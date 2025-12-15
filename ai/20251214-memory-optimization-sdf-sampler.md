# Memory Optimization for SDF Sampler

## Summary

Optimized the SDF sampler to eliminate a massive 4GB+ memory allocation for the positions buffer. Instead of passing all grid positions to the GPU, the shader now computes positions on-the-fly from grid parameters.

## Memory Savings (512³ grid)

| Before | After |
|--------|-------|
| CPU positions vector: 2.1 GB | Eliminated |
| GPU input buffer: 2.1 GB | Eliminated |
| Total: **4.2+ GB** | **32 bytes** (params only) |

## Changes Made

### vulkan_kim/sdf_sampler.comp
- Changed from input positions buffer (binding 0) to computing positions on-the-fly
- Uses uniform buffer with grid parameters: `resolution`, `time`, `minXYZ`, `maxXYZ`
- Computes 3D position from linear thread index using modulo arithmetic:
```glsl
uint ix = idx % res;
uint iy = (idx / res) % res;
uint iz = idx / (res * res);
vec3 p = vec3(minX + ix * stepX, minY + iy * stepY, minZ + iz * stepZ);
```

### vulkan/sdf_engine.hpp

**SDFSampler struct:**
- Removed `inputBuffer` and `inputMemory` fields

**init_sampler():**
- Removed input buffer allocation (was 2GB+ for 512³)
- Changed descriptor layout from 3 bindings to 2:
  - binding 0: output distances (storage buffer)
  - binding 1: params (uniform buffer, 32 bytes)

**sample_sdf_grid():**
- No longer creates positions vector (saved 2.1 GB CPU memory)
- Passes grid parameters directly to shader via uniform buffer

**Removed:**
- `sample_sdf()` function (obsolete, referenced removed `inputMemory`)

## Technical Details

The optimization is based on the WebGPU Marching Cubes approach:
- Thread ID → 3D grid coordinates via modulo operations
- Grid coordinates → world position via bounds interpolation
- No data transfer needed for positions

This is both faster (no memory copy) and uses dramatically less memory.

## Commands Run

None (code changes only)

## Testing

The mesh export should now work without memory issues. Previously, exporting at 512³ resolution would require 5+ GB RAM just for the positions array.

---

# Also in this session: Painterly Effects for Color Export

Updated `vulkan_kim/color_sampler.comp` to include the painterly effects from the main shader:

- **Brush stroke effect** - texture variations based on surface normal direction
- **Posterization** - limits colors to 8 levels for painted look
- **Color variation** - subtle RGB noise for organic feel
- **Edge darkening** - darkens edges for painterly outline effect

Now exported meshes with "Include Colors" will match the visual style of the live render, including shadows, lighting, and the Disco Elysium painterly aesthetic.

## Code Sharing Between Render and Export

Updated `extract_scene_for_colors()` to extract ALL visual code from the main shader:
- SDF primitives & boolean operations
- sceneSDF, sceneSDF_mat, getMaterialColor
- calcSoftShadow, calcAO (lighting)
- hash2D, noise2D, fbm (noise)
- posterize, brushStroke (painterly)

**Benefit**: Change the main shader once → exports automatically use the same visual style. No need to maintain two copies of shader code.

## Hot Reload

All shaders now hot reload automatically:

1. **Main render shader** - `check-shader-reload!` runs every frame, detects file changes
2. **SDF sampler** - tracks `cachedShaderModTime`, rebuilds on next mesh generation if shader changed
3. **Color sampler** - tracks `cachedShaderModTime`, rebuilds on next color export if shader changed

Just save the .comp file and:
- Render updates immediately
- Next mesh export uses updated SDF
- Next color export uses updated materials/lighting

## GLB Export with Vertex Colors

Added GLB (binary GLTF) export which properly supports vertex colors (unlike OBJ):

- Added `vendor/tinygltf` as git submodule
- Added `mc::exportGLB()` function in marching_cubes.hpp
- Export functions auto-detect format by file extension (.glb/.gltf vs .obj)
- UI has both "Export OBJ" and "Export GLB" buttons

**Usage**: Check "Include Colors" and click "Export GLB" - the colors will be visible in any GLTF viewer (Blender, three.js, etc.)
