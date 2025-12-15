# SDF to Mesh Conversion Methods - Research Notes

## Current Implementation

- **Method**: Marching Cubes with GPU SDF sampling
- **Issue**: Slow at high resolutions (1024^3 = 1 billion samples)
- **Quality**: Rounds off sharp corners/edges

## Method Comparison

| Method | Sharp Features | Speed | Quality | Complexity |
|--------|---------------|-------|---------|------------|
| Marching Cubes | Rounds corners | Fast | OK | Simple |
| **Dual Contouring** | Preserves edges | Similar | Better | Medium |
| Manifold DC | + watertight | Similar | Best | Higher |
| FlexiCubes | + differentiable | Similar | Best | NVIDIA-specific |
| Adaptive/Sparse | Same as base | Much faster | Same | Medium |

## Marching Cubes Limitations

> "Marching Cubes cannot do sharp edges and corners. Here's a square approximated with Marching Cubes. The corners have been sliced off."

> "When extracting mesh surfaces from a neural SDF, the marching cubes algorithm often fails to capture the full expressiveness of the neural SDF due to limitations like poor triangulation quality and misaligned feature lines."

## Dual Contouring - Recommended for Quality

### How It Works

Unlike Marching Cubes which places vertices on cell edges, Dual Contouring places one vertex **inside each cell** at the optimal position based on surface normals.

> "The dual contouring vertices will be placed at the point inside the cell that is most consistent with those gradients. Dual contouring has a more natural look and flow to it than marching cubes, and in 3D, this procedure is robust enough to pick points running along the edge of a sharp feature, and to pick out corners where they occur."

### Why It's Better for Sharp Features

> "The vertex position will be the intersection of the planes of all the crossing edges's position and normal - this keeps sharp edges sharp."

Uses **Quadratic Error Function (QEF)** to find optimal vertex placement:
- Extended Marching Cubes, Dual Contouring, and Manifold Dual Contouring leverage the gradient of the SDF as surface normal
- Identifies sharp corner points within a grid cell through QEF

### Requirements (We Already Have These!)

- SDF function (`sceneSDF`)
- Surface normals via gradient (`calcNormal`)
- GPU compute infrastructure

### Drawbacks

> "The main limitation is that meshes have self-intersections and can be non-manifold."

Solution: Use **Manifold Dual Contouring** which allows more than one vertex per cell to avoid non-manifold cases.

## Speed Optimizations

### 1. Sparse/Adaptive Sampling

**Problem**: Sampling ALL voxels (1024^3 = 1 billion) when most are far from surface.

**Solution**: Only sample near the surface using:
- **Octree subdivision** - Start coarse (64^3), subdivide only where SDF changes sign
- **Sparse voxel grids** - Store/compute fine voxels only near surface

### 2. GPU-Parallel Marching Cubes

> "GPU-parallel voxel scanning runs the same nearest-surface query, but splits the work across thousands of GPU threads for big speed-ups."

### 3. Hierarchical Approach

1. Sample at low resolution (128^3)
2. Identify cells near surface (|SDF| < threshold)
3. Only subdivide those cells to higher resolution
4. Repeat until desired detail

## Advanced Methods

### Occupancy-Based Dual Contouring (SIGGRAPH Asia 2024)

- State-of-the-art for implicit surfaces
- GPU-parallelized, learning-free
- Computation times of a few seconds
- Open source: https://github.com/KAIST-Visual-AI-Group/ODC

### Neural Dual Contouring (NDC)

- Uses neural network to predict vertex locations
- Works with signed/unsigned distance fields, binary voxels, or point clouds
- Better than traditional methods but requires training

### FlexiCubes (NVIDIA Kaolin)

- Differentiable variant of Dual Marching Cubes
- Enhances geometric fidelity through gradient-based optimization
- Part of NVIDIA Kaolin library

### SDF-CWF (2025)

- "Consolidating Weak Features in High-Quality Mesh Extraction"
- Addresses limitations of both MC and DC for weak features

## Implementation Options

### C++ Libraries

1. **libfive** - Uses Manifold Dual Contouring
   - https://libfive.com/
   - High quality, handles sharp features

2. **Custom Implementation**
   - Based on tutorials (see references)
   - More control, can integrate with existing GPU pipeline

3. **Check existing library**
   - See if `marching_cubes.hpp` has DC variant

### For Our Use Case (SDF mesh export with vertex colors)

**Recommended**: Dual Contouring because:
1. Already have SDF + normals (perfect for DC's Hermite data requirement)
2. Better edge preservation on sleeves, fingers, cigarette
3. Fewer triangles for same visual quality = faster color sampling

## References

### Tutorials
- [Dual Contouring Tutorial - Boris the Brave](https://www.boristhebrave.com/2018/04/15/dual-contouring-tutorial/)
- [Generating mesh from SDFs with Dual Contouring - Henrique Gois](https://henriquegois.dev/posts/generating-mesh-from-sdfs-with-dual-contouring/)

### Papers
- [Occupancy-Based Dual Contouring - SIGGRAPH Asia 2024](https://dl.acm.org/doi/10.1145/3680528.3687581)
- [Neural Dual Contouring - ACM TOG](https://dl.acm.org/doi/abs/10.1145/3528223.3530108)
- [SDF-CWF: High-Quality Mesh Extraction](https://www.sciencedirect.com/science/article/abs/pii/S0010448525000739)

### Libraries/Code
- [ODC GitHub - SIGGRAPH Asia 2024](https://github.com/KAIST-Visual-AI-Group/ODC)
- [NVIDIA Kaolin - FlexiCubes](https://kaolin.readthedocs.io/en/latest/modules/kaolin.ops.conversions.html)
- [libfive - Manifold Dual Contouring](https://libfive.com/)

## Implementation Status

### Dual Contouring - IMPLEMENTED

Added `mc::generateMeshDC()` to `vulkan/marching_cubes.hpp`:

**Features:**
- QEF (Quadratic Error Function) solver for optimal vertex placement
- Finite difference normals from SDF grid (Option A - no GPU changes needed)
- Same interface as `generateMesh()` - drop-in replacement
- Clamping to cell bounds for stability

**Usage from nREPL:**
```clojure
;; Enable Dual Contouring
(sdfx/set_mesh_use_dual_contouring true)

;; Generate mesh (will use DC)
(sdfx/generate_mesh_preview (cpp/int. 512))

;; Check current setting
(sdfx/get_mesh_use_dual_contouring)
```

**Key Functions Added:**
- `mc::generateMeshDC()` - Main DC mesh generation
- `mc::computeNormalFromGrid()` - Finite difference normals
- `mc::QEF` - Quadratic Error Function solver struct
- `get_mesh_use_dual_contouring()` / `set_mesh_use_dual_contouring()` - Engine API

## Next Steps

1. **Test DC**: Compare quality vs marching cubes at same resolution
2. **For Speed**: Implement adaptive/hierarchical sampling
3. **Optimize DC**: Add multi-threading (like MC has)
