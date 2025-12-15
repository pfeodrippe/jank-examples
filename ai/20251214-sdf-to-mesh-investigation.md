# SDF to Mesh Generation - Investigation & Implementation Plan

**Date**: 2024-12-14
**Goal**: Generate meshes (OBJ/GLTF) from SDFs at runtime for caching and reuse

---

## Executive Summary

**Verdict: YES, this is absolutely possible and well-established.**

The approach involves:
1. Sampling the SDF on a 3D grid (CPU or GPU compute shader)
2. Running a meshing algorithm (Marching Cubes, Dual Contouring, or Surface Nets)
3. Exporting to OBJ/GLTF format
4. Caching for reuse

---

## Research Findings

### Meshing Algorithms Comparison

| Algorithm           | Sharp Features      | Speed  | Manifold | Complexity |
|---------------------|---------------------|--------|----------|------------|
| **Marching Cubes**  | No (rounds corners) | Fast   | Yes      | Simple     |
| **Dual Contouring** | Yes                 | Medium | No*      | Complex    |
| **Surface Nets**    | No                  | Fast   | No*      | Simple     |
| **Manifold DC**     | Yes                 | Medium | Yes      | Complex    |

*Can produce self-intersections or non-manifold geometry

### Recommended: Start with Marching Cubes

For most SDFs (organic shapes, smooth surfaces), Marching Cubes is ideal:
- Simple to implement
- Guaranteed watertight meshes
- No self-intersections
- Many header-only C++ implementations available

If sharp features matter (CAD-like models with hard edges), upgrade to Dual Contouring later.

---

## Algorithm Deep Dive

### 1. Marching Cubes

**How it works:**
1. Sample SDF on a regular 3D grid (e.g., 128x128x128)
2. For each cube of 8 adjacent samples:
   - Determine which corners are inside (negative) vs outside (positive)
   - Use 256-entry lookup table to get triangle configuration
   - Interpolate vertex positions along edges where sign changes

**Pros:**
- Simple, well-understood
- Always produces manifold meshes
- ~20M triangles/sec on modern CPUs

**Cons:**
- Cannot preserve sharp corners (they get rounded)
- Higher resolution needed for detail

**Best C++ library**: [MarchingCubeCpp](https://github.com/aparis69/MarchingCubeCpp)
- Header-only, public domain
- Zero dependencies
- Returns indexed mesh (vertices, normals, indices)

### 2. Dual Contouring

**How it works:**
1. Sample SDF AND its gradient on a grid
2. Place one vertex per cell, positioned to minimize error from gradient normals
3. Connect vertices across cells where sign changes occur

**Pros:**
- Preserves sharp features (edges, corners)
- Fewer triangles for same quality

**Cons:**
- Requires gradient information
- Can produce self-intersections
- More complex implementation

**Best library**: [libfive](https://github.com/libfive/libfive) (MPL-2.0)
- Production-quality Manifold Dual Contouring
- Used by Studio (CAD software)
- More complex to integrate

### 3. Surface Nets

**How it works:**
- Like Dual Contouring but simpler vertex placement
- Vertex = center of mass of edge crossings (no gradient needed)

**Pros:**
- Simpler than Dual Contouring
- Smoother than Marching Cubes
- Fast implementation exists in Rust (20M tri/sec)

**Best implementation**: Roll your own or port [fast-surface-nets-rs](https://github.com/bonsairobo/fast-surface-nets-rs)

---

## Current Codebase Architecture

### SDF System Overview

```
src/vybe/sdf/
  ├── sdf.jank          # Main entry point
  ├── math.jank         # Vector/matrix ops
  ├── render.jank       # Vulkan rendering interface
  ├── shader.jank       # Shader hot-reload
  ├── state.jank        # Camera/object state
  └── screenshot.jank   # PNG export

vulkan/
  └── sdf_engine.hpp    # C++ Vulkan backend (2100+ lines)

vulkan_kim/
  ├── sdf_scene.comp    # GLSL compute shader with SDF primitives
  └── hand_cigarette.comp
```

### Key SDF Primitives Already Implemented (GLSL)

```glsl
// Primitives
sdSphere, sdBox, sdRoundBox, sdEllipsoid
sdCapsule, sdCylinder, sdTorus, sdPlane

// Boolean ops
opUnion, opSubtract, opIntersect
opSmoothUnion, opSmoothSubtract

// Transforms
rotateX, rotateY, rotateZ
```

### Scene Object System

```cpp
// sdf_engine.hpp
struct SceneObject {
    float position[3];
    float rotation[3];
    int type;
};
// MAX_OBJECTS = 32 per scene
```

---

## Implementation Plan

### Phase 1: CPU-side SDF Evaluation (Foundation)

**Goal**: Create C++ functions that mirror GLSL SDF primitives

```cpp
// sdf_eval.hpp - Header-only SDF evaluation
namespace sdf {

float sphere(vec3 p, float r) {
    return length(p) - r;
}

float box(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f);
}

// ... mirror all GLSL primitives
}
```

**Deliverables:**
- `vulkan/sdf_eval.hpp` - CPU-side SDF evaluation
- Same API as GLSL for consistency
- Scene evaluation function: `float evalScene(vec3 p, SceneObject* objects, int count)`

### Phase 2: Marching Cubes Integration

**Goal**: Generate mesh from SDF

**Option A: Use MarchingCubeCpp (recommended)**
```cpp
#define MC_CPP_IMPLEMENTATION
#include "marching_cube.hpp"

void generateMesh(
    std::function<float(float,float,float)> sdf,
    vec3 boundsMin, vec3 boundsMax,
    int resolution,
    std::vector<float>& vertices,
    std::vector<float>& normals,
    std::vector<uint32_t>& indices
) {
    MC::MC_FLOAT* grid = sampleSDF(sdf, boundsMin, boundsMax, resolution);
    MC::marching_cube(grid, resolution, resolution, resolution,
                      vertices, normals, indices);
}
```

**Option B: GPU Compute Marching Cubes**
- Keep SDF evaluation on GPU
- Use atomic operations to build vertex buffer
- More complex but faster for high-resolution

**Deliverables:**
- `vulkan/mesh_generator.hpp` - Mesh generation from SDF
- Configurable resolution (16x16x16 to 256x256x256)
- Bounding box auto-detection or manual specification

### Phase 3: Mesh Export (OBJ/GLTF)

**OBJ Export (simple, text-based)**
```cpp
void exportOBJ(const char* path,
               const std::vector<float>& vertices,
               const std::vector<float>& normals,
               const std::vector<uint32_t>& indices) {
    FILE* f = fopen(path, "w");
    // Write vertices
    for (size_t i = 0; i < vertices.size(); i += 3)
        fprintf(f, "v %f %f %f\n", vertices[i], vertices[i+1], vertices[i+2]);
    // Write normals
    for (size_t i = 0; i < normals.size(); i += 3)
        fprintf(f, "vn %f %f %f\n", normals[i], normals[i+1], normals[i+2]);
    // Write faces (1-indexed)
    for (size_t i = 0; i < indices.size(); i += 3)
        fprintf(f, "f %d//%d %d//%d %d//%d\n",
                indices[i]+1, indices[i]+1,
                indices[i+1]+1, indices[i+1]+1,
                indices[i+2]+1, indices[i+2]+1);
    fclose(f);
}
```

**GLTF Export (binary, modern)**

Use [tinygltf](https://github.com/syoyo/tinygltf) (header-only, MIT):
```cpp
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

void exportGLTF(const char* path, /* mesh data */) {
    tinygltf::Model model;
    tinygltf::TinyGLTF gltf;
    // Build accessors, bufferViews, buffers...
    gltf.WriteGltfSceneToFile(&model, path, true, true, true, true);
}
```

**Deliverables:**
- `vulkan/mesh_export.hpp` - OBJ and GLTF export
- OBJ for debugging (human-readable)
- GLB (binary GLTF) for production (smaller, faster)

### Phase 4: Caching System

**Cache Key Generation**
```cpp
uint64_t computeCacheKey(
    const SceneObject* objects, int count,
    int resolution,
    vec3 boundsMin, vec3 boundsMax
) {
    // Hash all parameters that affect mesh output
    XXH64_state_t* state = XXH64_createState();
    XXH64_update(state, objects, count * sizeof(SceneObject));
    XXH64_update(state, &resolution, sizeof(int));
    XXH64_update(state, &boundsMin, sizeof(vec3));
    XXH64_update(state, &boundsMax, sizeof(vec3));
    uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}
```

**Cache Storage**
```
cache/
  └── meshes/
      ├── a1b2c3d4e5f6.glb    # Hash-named mesh files
      ├── a1b2c3d4e5f6.meta   # Metadata (source SDF params)
      └── ...
```

**Cache API**
```cpp
class MeshCache {
public:
    bool has(uint64_t key);
    Mesh* get(uint64_t key);           // Load from disk
    void put(uint64_t key, Mesh* m);   // Save to disk
    void clear();                       // Invalidate all
};
```

**Deliverables:**
- `vulkan/mesh_cache.hpp` - Disk-based mesh cache
- LRU eviction (optional)
- Memory-mapped loading for speed

### Phase 5: Jank Integration

```clojure
;; src/vybe/sdf/mesh.jank

(defn generate-mesh
  "Generate mesh from current SDF scene"
  [{:keys [resolution bounds]}]
  (let* [res (or resolution 64)
         bounds (or bounds (auto-bounds))
         cache-key (compute-cache-key res bounds)]
    (if (cache-has? cache-key)
      (cache-get cache-key)
      (let* [mesh (cpp/sdf-to-mesh res bounds)]
        (cache-put cache-key mesh)
        mesh))))

(defn export-mesh
  "Export mesh to file"
  [mesh path & {:keys [format] :or {format :glb}}]
  (case format
    :obj (cpp/export-obj mesh path)
    :glb (cpp/export-glb mesh path)
    :gltf (cpp/export-gltf mesh path)))

(defn mesh->vulkan
  "Upload mesh to Vulkan for rendering"
  [mesh]
  (cpp/upload-mesh-to-gpu mesh))
```

---

## File Format Recommendations

| Format            | Use Case                 | Size     | Speed   | Features          |
|-------------------|--------------------------|----------|---------|-------------------|
| **OBJ**           | Debug, import to Blender | Large    | Slow    | Simple            |
| **GLB**           | Production cache         | Small    | Fast    | Binary, materials |
| **Custom Binary** | Hot path                 | Smallest | Fastest | No overhead       |
|                   |                          |          |         |                   |

**Recommendation**:
- GLB for exported files (compatible with everything)
- Custom binary for runtime cache (just vertices/normals/indices + header)

---

## Performance Estimates

| Operation      | Time (64^3 grid) | Time (128^3 grid) |
|----------------|------------------|-------------------|
| SDF sampling   | ~10ms            | ~80ms             |
| Marching Cubes | ~5ms             | ~40ms             |
| Export to GLB  | ~2ms             | ~15ms             |
| Cache lookup   | <1ms             | <1ms              |
| GPU upload     | ~1ms             | ~5ms              |
|                |                  |                   |

**Total first-time generation**: ~20-150ms depending on resolution
**Cached load**: <5ms

---

## External Dependencies

### Required (header-only)
1. **MarchingCubeCpp** - [GitHub](https://github.com/aparis69/MarchingCubeCpp) (MIT)
   - Drop-in `marching_cube.hpp`

2. **tinygltf** - [GitHub](https://github.com/syoyo/tinygltf) (MIT)
   - For GLTF/GLB export
   - Also needs `json.hpp` and `stb_image*.h` (already have stb)

### Optional
3. **xxHash** - For fast cache key hashing (can use std::hash instead)
4. **libfive** - If sharp feature preservation needed later

---

## Risk Assessment

| Risk                              | Impact                       | Mitigation                          |
|-----------------------------------|------------------------------|-------------------------------------|
| SDF evaluation differs CPU vs GPU | Mesh doesn't match rendering | Use identical math, test thoroughly |
| Marching Cubes artifacts          | Visual seams/holes           | Increase resolution, add smoothing  |
| Cache invalidation                | Stale meshes                 | Include version in cache key        |
| Large meshes fill disk            | Storage issues               | LRU eviction, max cache size        |

---

## Next Steps

1. **Immediate**: Add `marching_cube.hpp` to `vulkan/` directory
2. **Day 1**: Implement CPU SDF evaluation matching GLSL
3. **Day 2**: Wire up Marching Cubes, export first OBJ
4. **Day 3**: Add tinygltf, implement GLB export
5. **Day 4**: Build caching system
6. **Day 5**: Jank integration and testing

---

## References

### Algorithms
- [Marching Cubes Tutorial](https://graphics.stanford.edu/~mdfisher/MarchingCubes.html) - Stanford
- [Dual Contouring Tutorial](https://www.boristhebrave.com/2018/04/15/dual-contouring-tutorial/) - Boris the Brave
- [Interactive MC/DC Explanation](https://wordsandbuttons.online/interactive_explanation_of_marching_cubes_and_dual_contouring.html)
- [Surface Nets Explanation](https://cerbion.net/blog/understanding-surface-nets/)

### Libraries
- [MarchingCubeCpp](https://github.com/aparis69/MarchingCubeCpp) - Header-only MC
- [tinygltf](https://github.com/syoyo/tinygltf) - GLTF loader/writer
- [libfive](https://github.com/libfive/libfive) - Production SDF library
- [MeshLib](https://meshlib.io/feature/mesh-to-sdf/) - Commercial option

### Research Papers
- [Marching Cubes Original (1987)](https://dl.acm.org/doi/10.1145/37402.37422)
- [Dual Contouring (2002)](https://www.cs.rice.edu/~jwarren/papers/dualcontour.pdf)
- [Manifold Dual Contouring](https://people.engr.tamu.edu/schaefer/research/dualsimp_tvcg.pdf)

---

## Commands Used

```bash
# Explored codebase
find src -name "*.jank"
grep -r "sdf\|distance\|mesh" src/
ls -la vulkan_kim/*.comp

# Read key files
cat vulkan/sdf_engine.hpp
cat vulkan_kim/sdf_scene.comp

# Web research
# - Marching Cubes implementations
# - Dual Contouring vs MC comparison
# - GLTF export libraries
# - Mesh caching strategies
```
