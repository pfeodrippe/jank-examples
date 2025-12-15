# Plan: Mesh Decimation and LOD Generation

## Overview
Add mesh decimation (triangle reduction) and LOD (Level of Detail) generation to the SDF mesh export pipeline.

## Goals
1. Reduce polygon count while preserving visual quality
2. Generate multiple LOD levels for game engine use
3. Export individual LODs or all LODs at once

## Implementation Approach

### Algorithm: Quadric Error Metrics (QEM)
The industry-standard approach for mesh decimation:
- Assign error quadric to each vertex based on surrounding face planes
- Iteratively collapse edges with lowest error cost
- Preserves surface shape and sharp features
- O(n log n) complexity with priority queue

### Files to Modify

| File | Changes |
|------|---------|
| `vulkan/marching_cubes.hpp` | Add `decimateMesh()` and `generateLODs()` functions |
| `vulkan/sdf_engine.hpp` | Add `export_mesh_lods()` function, LOD caching |
| `src/vybe/sdf/ui.jank` | Add LOD controls and export buttons |

## Detailed Implementation

### 1. Core Decimation (marching_cubes.hpp)

```cpp
// Quadric error matrix for vertex placement
struct Quadric {
    float a[10];  // Symmetric 4x4 matrix (upper triangle)
    // Methods: add, evaluate, solve
};

// Decimation function
Mesh decimateMesh(const Mesh& mesh, float targetRatio);  // 0.0-1.0
Mesh decimateMesh(const Mesh& mesh, int targetTriangles);

// LOD generation
struct LODSet {
    std::vector<Mesh> levels;  // LOD0 = highest detail
    std::vector<float> ratios; // e.g., {1.0, 0.5, 0.25, 0.1}
};

LODSet generateLODs(const Mesh& mesh, const std::vector<float>& ratios);
```

**Decimation Algorithm Steps:**
1. Build adjacency structure (half-edge or simple vertex-face)
2. Compute initial quadric for each vertex from incident faces
3. For each edge, compute collapse cost and optimal vertex position
4. Use priority queue (min-heap) to process edges by cost
5. Collapse lowest-cost edge, update affected quadrics
6. Repeat until target triangle count reached
7. Rebuild index buffer, compact vertex array

### 2. Engine Integration (sdf_engine.hpp)

```cpp
// LOD export result
struct LODExportResult {
    bool success;
    int lodCount;
    std::vector<size_t> vertexCounts;
    std::vector<size_t> triangleCounts;
    const char* message;
};

// Export functions
LODExportResult export_mesh_with_lods(
    const char* basePath,      // e.g., "model" -> "model_lod0.glb", etc.
    int resolution,
    bool includeColors,
    const std::vector<float>& lodRatios  // e.g., {1.0, 0.5, 0.25}
);

// Single decimated export
MeshExportResult export_decimated_mesh(
    const char* filepath,
    int resolution,
    float targetRatio,
    bool includeColors
);
```

### 3. UI Controls (ui.jank)

New UI elements in mesh export section:
- **LOD Presets dropdown**: "Game (4 LODs)", "Mobile (3 LODs)", "Custom"
- **Target ratio slider**: 0.1 to 1.0 (for single decimation)
- **Export buttons**:
  - "Export All LODs" - exports model_lod0.glb, model_lod1.glb, etc.
  - "Export Decimated" - single file at target ratio
- **Preview LOD selector**: Switch between LOD levels in viewport

### 4. Default LOD Ratios

| Preset | LOD0 | LOD1 | LOD2 | LOD3 |
|--------|------|------|------|------|
| Game   | 100% | 50%  | 25%  | 10%  |
| Mobile | 100% | 30%  | 10%  | -    |
| Cinematic | 100% | 75% | 50% | 25% |

## Edge Cases & Considerations

1. **Attribute preservation**: Interpolate normals, colors, UVs at collapsed vertices
2. **Boundary edges**: Don't collapse edges on mesh boundaries
3. **Degenerate triangles**: Skip collapses that create zero-area faces
4. **Manifold preservation**: Ensure mesh stays watertight after decimation
5. **Memory**: Large meshes may need chunked processing

## Testing Plan

1. Test decimation at various ratios (0.9, 0.5, 0.25, 0.1)
2. Verify normals are correctly interpolated
3. Verify vertex colors preserved
4. Compare visual quality DC vs MC at same triangle count
5. Performance test: decimation time for 10k, 50k, 100k triangle meshes

## Estimated Scope

- **Quadric struct + operations**: ~50 lines
- **Decimation algorithm**: ~200 lines
- **LOD generation wrapper**: ~30 lines
- **Engine integration**: ~100 lines
- **UI additions**: ~50 lines

**Total: ~430 lines of C++ code + ~50 lines jank**

## Status
- [ ] Not started - plan only
