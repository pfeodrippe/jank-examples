# Fill With Cubes Implementation

## Summary
Added "Fill With Cubes" option to the Dual Contouring (DC) mesh generation with adjustable voxel size. This creates solid voxel-style cubes for each active cell instead of DC surface quads, which fixes slicing artifacts visible at high resolutions (1024+).

## Problem
At high resolutions (1024), the DC mesh showed "slicing" artifacts where geometry appeared cut through. This is due to how DC places vertices and generates quads at cell boundaries.

## Solution
Added an optional "Fill With Cubes" mode that generates 8 vertices and 12 triangles (6 faces) per active cell, creating a solid voxel appearance instead of surface quads.

## Files Modified

### vulkan/marching_cubes.hpp
- Modified `generateMeshDC()` signature to accept `fillWithCubes` and `voxelSize` parameters
- Added parallel cube generation mode:
  - Each thread has its own mesh buffer (ThreadCubeMesh)
  - Generates 8 vertices per active cell (cube corners)
  - Generates 12 triangles (6 faces x 2 triangles) per cube
  - Thread-local buffers merged at end
  - Cube size scales around cell center based on `voxelSize` parameter

### vulkan/sdf_engine.hpp
- Added `meshFillWithCubes` state variable (line 239)
- Added `meshVoxelSize` float variable (default 1.0)
- Added getter/setter functions for both parameters
- Modified call site to pass both parameters to `generateMeshDC()`

### src/vybe/sdf/ui.jank
- Added `*fill-with-cubes` atom for checkbox state
- Added `*voxel-size` atom for slider value (default 1.0)
- Added checkbox UI that appears when "Dual Contouring" is enabled
- Added "Voxel Size" slider (0.1 to 2.0) that appears when "Fill With Cubes" is enabled

## Key Code

```cpp
// marching_cubes.hpp - Cube generation with voxel size
// Cell center and scaled half-size
Vec3 center = {
    bounds_min.x + (x + 0.5f) * cell_size.x,
    bounds_min.y + (y + 0.5f) * cell_size.y,
    bounds_min.z + (z + 0.5f) * cell_size.z
};
float hx = cell_size.x * 0.5f * voxelSize;
float hy = cell_size.y * 0.5f * voxelSize;
float hz = cell_size.z * 0.5f * voxelSize;
Vec3 cmin = {center.x - hx, center.y - hy, center.z - hz};
Vec3 cmax = {center.x + hx, center.y + hy, center.z + hz};
// Generate 8 vertices and 12 triangles...
```

```jank
;; ui.jank - Checkbox and slider (only visible when DC enabled)
(when (u/p->v *use-dual-contouring)
  (imgui/Checkbox "Fill With Cubes" (cpp/unbox *fill-with-cubes))
  (imgui/SameLine)
  (imgui/TextDisabled "(voxel style)")
  (sdfx/set_mesh_fill_with_cubes (cpp/bool. (u/p->v *fill-with-cubes)))
  ;; Voxel size slider (only when fill with cubes is enabled)
  (when (u/p->v *fill-with-cubes)
    (imgui/SliderFloat "Voxel Size" (cpp/unbox *voxel-size) (cpp/float. 0.1) (cpp/float. 2.0))
    (sdfx/set_mesh_voxel_size (u/p->v *voxel-size))))
```

## Test Results

Build and run successful on M3 Max:
```
DC mesh (cubes): 6536 vertices, 9804 triangles (threads: 16)   @ res 384
DC mesh (cubes): 28024 vertices, 42036 triangles (threads: 16) @ res 768
DC mesh (cubes): 56752 vertices, 85128 triangles (threads: 16) @ res 1088
```

## Usage

1. Enable "Dual Contouring" checkbox
2. Enable "Fill With Cubes" checkbox (appears below DC when DC is on)
3. Adjust "Voxel Size" slider (appears when Fill With Cubes is on):
   - **1.0** = default, cubes fill the cell exactly
   - **< 1.0** = smaller cubes with gaps between them
   - **> 1.0** = larger cubes that overlap (can look more solid)
4. The mesh will regenerate automatically when parameters change

## Trade-offs

| Mode | Pros | Cons |
|------|------|------|
| DC (default) | Fewer triangles, smooth surfaces | Slicing artifacts at high res |
| Fill With Cubes | No slicing, solid voxel look | More triangles, blocky appearance |

## Commands Used

```bash
# Build and run
make sdf
```

## Next Steps
- Consider adding cube smoothing/averaging for less blocky look
- Evaluate mesh decimation to reduce cube mode triangle count
