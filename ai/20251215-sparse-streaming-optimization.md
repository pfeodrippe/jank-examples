# Sparse Streaming SDF Optimization - 2024-12-15

## Problem
At resolution 1024, the full SDF sampling took ~4750ms + ~700ms DC = ~5450ms total.
The main bottleneck was sampling 1 billion SDF points when only ~0.1% contain surface.

## Research Findings

From web research on GPU SDF optimization:
- [Fast Narrow Band SDF Generation on GPUs](https://al-ro.github.io/projects/sdf/) - Only compute near surface
- [NVIDIA Thread-Group ID Swizzling](https://developer.nvidia.com/blog/optimizing-compute-shaders-for-l2-locality-using-thread-group-id-swizzling/) - L2 cache optimization
- [hp-Adaptive SDF Octree](https://github.com/jw007123/hp-Adaptive-Signed-Distance-Field-Octree) - Orders of magnitude faster queries
- [Occupancy-Based Dual Contouring](https://arxiv.org/html/2409.13418v1) - GPU-parallel DC

Key insight: **Only evaluate SDF where surface exists** (narrow band approach).

## Solution: Sparse Streaming

Instead of sampling all 1024³ points, use a two-level approach:

1. **Coarse Sampling (128³)** - 2M points, ~46ms
   - Fast initial pass to find surface regions
   - Check for sign changes at cell corners

2. **Super-Cell Detection**
   - Group 4x4x4 coarse cells into super-cells
   - Mark super-cells that contain sign changes (surface crossings)
   - Typically finds ~9 surface regions out of 4096 super-cells

3. **Fine Sampling Per Region** - Only surface regions
   - Each super-cell = 33³ fine points = ~36K points
   - Sample + run DC + merge mesh
   - Total ~300K points vs 1 billion (99.97% reduction!)

## Implementation

New function `generate_mesh_sparse_streaming()` in `vulkan/sdf_engine.hpp`:

```cpp
inline mc::Mesh generate_mesh_sparse_streaming(
    int resolution,
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    bool fillWithCubes = true,
    float voxelSize = 1.0f,
    float isolevel = 0.0f
) {
    // 1. Coarse sample at 1/8 resolution (1024 -> 128)
    int coarseRes = std::max(64, resolution / 8);

    // 2. Find super-cells with sign changes
    // Super-cell = 4x4x4 coarse cells

    // 3. For each surface super-cell:
    //    - Sample fine grid for that region
    //    - Run DC to generate mesh
    //    - Merge into final mesh
}
```

Surface detection:
```cpp
// Check for sign change at cell corners
float minVal = +INF, maxVal = -INF;
for (each corner) {
    minVal = min(minVal, distance[corner]);
    maxVal = max(maxVal, distance[corner]);
}
// Has surface if sign change
if (minVal <= 0 && maxVal >= 0) hasSurface = true;
```

## Results

| Resolution | Old Time | New Time | Speedup | Vertices |
|------------|----------|----------|---------|----------|
| 1024 | ~5450 ms | **216 ms** | **25x** | 50064 |
| 1536 | N/A | ~600 ms | - | 112848 |

### Breakdown at 1024 resolution:
- Coarse sampling (128³): 46 ms
- Fine sampling (9 regions × 33³): ~15 ms
- DC cubes generation: ~15 ms
- Mesh merging: ~1 ms
- **Total: 216 ms**

## Files Modified

| File | Changes |
|------|---------|
| `vulkan/sdf_engine.hpp` | Added `generate_mesh_sparse_streaming()`, modified `generate_mesh_preview()` to use it for GPU DC at res > 256 |
| `vulkan_kim/sdf_sampler_sparse.comp` | New shader (not used yet - for future indirect dispatch) |
| `vulkan_kim/sdf_find_active.comp` | New shader (not used yet - for future GPU-based region detection) |
| `vulkan_kim/dc_cubes_fused.comp` | New shader (not used yet - for fused SDF+DC) |

## Integration

`generate_mesh_preview()` now automatically uses sparse streaming for:
- GPU DC mode (`meshUseGpuDC = true`)
- Resolution > 256
- Falls back to standard approach if sparse streaming fails

## Future Optimizations

1. **GPU-based region detection** - Use `sdf_find_active.comp` to find surface regions on GPU
2. **Indirect dispatch** - Use `sdf_sampler_sparse.comp` with indirect dispatch for truly sparse sampling
3. **Fused SDF+DC** - Use `dc_cubes_fused.comp` to eliminate intermediate distance buffer entirely

## Commands

```bash
# Build and test
make sdf

# Expected output at 1024 resolution:
# Sparse streaming: 9 surface regions, coarse 128³ (46 ms)
# DC mesh (sparse streaming): 50064 vertices, 75096 triangles (9 regions, 15 ms process, 216 ms total)
```
