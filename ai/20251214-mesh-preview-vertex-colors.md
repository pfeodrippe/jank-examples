# Mesh Preview Vertex Colors Implementation

## Summary

Added support for displaying sampled vertex colors in the mesh preview viewer when "Include Colors" is checked and mesh is regenerated.

**Also added hot reload for mesh shaders** - changes to mesh.vert.spv and mesh.frag.spv are automatically detected and the pipeline is recreated.

## Changes Made

### vulkan_kim/mesh.vert
- Already had `inColor` (location 2) and `fragColor` output from previous session

### vulkan_kim/mesh.frag
- Added `layout(location = 2) in vec3 fragColor` input
- Added `vec4 options` to the MeshUBO (5th field after resolution)
- Uses `ubo.options.x > 0.5` to check if vertex colors should be used
- When vertex colors are enabled and non-zero, passes them through directly (they already include lighting from color sampler)
- Falls back to cyan color with lighting when colors disabled

### vulkan/sdf_engine.hpp

**MeshVertex struct** (line 3527):
```cpp
struct MeshVertex {
    float pos[3];
    float normal[3];
    float color[3];  // NEW
};
```

**UBO struct** (line 70):
```cpp
struct UBO {
    float cameraPos[4];
    float cameraTarget[4];
    float lightDir[4];
    float resolution[4];
    float options[4];       // NEW: x=useVertexColors
    float editMode[4];      // shifted
    // ... rest unchanged
};
```

**Vertex attribute bindings** (line 3673):
- Changed from 2 attributes to 3
- Added `{2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, color)}`

**Engine state**:
- Added `meshUseVertexColors` flag (line 234)
- Added `get_mesh_use_vertex_colors()` function
- Added `set_mesh_use_vertex_colors(bool)` function - sets `meshNeedsRegenerate = true`

**upload_mesh_preview()** (line 3755):
- Added optional `colors` parameter: `const std::vector<mc::Color3>& colors = {}`
- Populates vertex colors from the colors vector if provided

**generate_mesh_preview()** (line 3908):
- When `meshUseVertexColors` is true, samples colors via `sample_vertex_colors()`
- Passes colors to `upload_mesh_preview()`

**UBO update** (line 1706):
- Added `ubo.options[0] = e->meshUseVertexColors ? 1.0f : 0.0f`

### vulkan_kim/sdf_scene.comp & hand_cigarette.comp
- Added `vec4 options` to UBO between resolution and editMode to match C++ struct

### src/vybe/sdf/ui.jank
- Syncs "Include Colors" checkbox with `set_mesh_use_vertex_colors()`
- Added auto-regeneration when `mesh_preview_needs_regenerate()` returns true

## How It Works

1. User checks "Include Colors" checkbox
2. `set_mesh_use_vertex_colors(true)` is called, which sets `meshNeedsRegenerate = true`
3. UI loop detects `mesh_preview_needs_regenerate()` and calls `generate_mesh_preview()`
4. `generate_mesh_preview()` samples colors via GPU compute shader (`sample_vertex_colors()`)
5. Colors are uploaded to GPU with vertex data in `upload_mesh_preview()`
6. Fragment shader reads `ubo.options.x` and uses vertex colors when enabled

## Testing

Run `make sdf` and:
1. Click "Show Mesh" button
2. Check "Include Colors" checkbox
3. Click "Regenerate Mesh"
4. Mesh should show with sampled colors from the SDF shader

## Notes

- Colors include full lighting/shadows/painterly effects from color_sampler.comp
- When colors are disabled (checkbox unchecked), mesh shows default cyan color
- Both preview and export now share the same color flag

## Hot Reload for Mesh Shaders

Added hot reload support for mesh shaders (mesh.vert, mesh.frag):

**New functions in sdf_engine.hpp:**
- `invalidate_mesh_pipeline()` - destroys the mesh pipeline to force recreation
- `get_mesh_shader_mod_time()` - returns max mod time of mesh.vert.spv/mesh.frag.spv
- `check_mesh_shader_reload()` - checks if shaders changed and reloads pipeline

**Engine state:**
- Added `lastMeshShaderModTime` to track modification times

**Render loop (sdf.jank):**
- Added `(sdfx/check_mesh_shader_reload)` call after main shader reload check

**To recompile mesh shaders:**
```bash
cd vulkan_kim
glslangValidator -V mesh.vert -o mesh.vert.spv
glslangValidator -V mesh.frag -o mesh.frag.spv
```

The pipeline will automatically reload on the next frame.
