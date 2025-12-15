# Color Sampling from SDF Shader for Mesh Export

## Summary

Implemented GPU-based vertex color sampling for mesh export. Now when "Include Colors" is checked, the export will sample actual shader colors (lighting, shadows, materials) at each vertex position using the same shader code as the main renderer.

## Changes Made

### New Files
- `vulkan_kim/color_sampler.comp` - Compute shader template for color sampling at vertex positions

### Modified Files

**vulkan/sdf_engine.hpp** - Added:
- `ColorSampler` struct (similar to SDFSampler)
- `get_color_sampler()` - singleton accessor
- `extract_scene_for_colors()` - extracts sceneSDF, sceneSDF_mat, getMaterialColor from shader
- `build_color_sampler_shader()` - builds sampler shader with scene code
- `cleanup_color_sampler()` - cleanup resources
- `init_color_sampler()` - initialize Vulkan resources for color sampling
- `sample_vertex_colors()` - samples colors at mesh vertex positions using GPU

Modified export functions to use GPU color sampling instead of default gray.

**vulkan/marching_cubes.hpp** - Fixed:
- Flipped triangle winding order in `exportOBJ()` for correct OBJ convention (CCW = front face)
- Changed from `i0, i1, i2` to `i0, i2, i1` to fix backface rendering

## How Color Sampling Works

1. After mesh generation and normal computation, `sample_vertex_colors()` is called
2. Vertex positions and normals are uploaded to GPU buffers
3. The color sampler shader computes for each vertex:
   - Material lookup via `sceneSDF_mat()`
   - Base color via `getMaterialColor()`
   - Diffuse lighting with soft shadows
   - Ambient occlusion
   - Rim lighting
   - Specular highlights
   - **Painterly effects** (brush strokes, posterization, color variation)
   - **Edge darkening**
4. Colors are read back and stored in mesh

The painterly effects match the main shader's visual style, giving exported meshes the same artistic look as the live render.

## Shader Extraction

The `extract_scene_for_colors()` function extracts:
- SDF primitives (sdSphere, sdBox, etc.)
- Boolean operations (opUnion, opSmoothUnion, etc.)
- Rotation matrices
- All scene-specific code up to RAYMARCHING section
- `sceneSDF()`, `sceneSDF_mat()`, `getMaterialColor()` functions

Falls back to default implementations if functions aren't found.

## Usage

In the SDF Debug UI:
1. Check "Include Colors" checkbox
2. Click "Export OBJ"
3. The exported mesh will have vertex colors matching the rendered view

## Commands Run

None (code changes only, recompilation happens at runtime via shaderc)

## Next Steps

- Test with different shaders to ensure extraction works for all scene types
- Consider adding option to bake shadows/AO into colors vs using real-time lighting
