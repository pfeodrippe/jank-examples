# Mesh Export Fixes - Dec 14, 2025

## Issues Fixed

### 1. Locale Bug in OBJ Export
**Problem**: OBJ face indices had thousands separators (commas) due to locale settings:
```
f 9,061 9,062 9,063  # WRONG
```

**Fix**: Added `file.imbue(std::locale::classic())` to `marching_cubes.hpp`:
```cpp
std::ofstream file(filename);
file.imbue(std::locale::classic());  // Use C locale
```

Also added `#include <locale>` to the file.

### 2. Shared Mesh Between Preview and Export
**Problem**: Preview and export were generating meshes separately, duplicating work.

**Fix**: Added stored mesh in Engine struct:
```cpp
mc::Mesh currentMesh;
int currentMeshResolution = 0;
```

- `generate_mesh_preview()` now stores the mesh in `e->currentMesh`
- `export_scene_mesh_gpu()` reuses the stored mesh if resolution matches
- Output now shows: "Exporting stored mesh (resolution X)..."

### 3. Mesh Scale Clarification
The exported mesh vertices appeared "too small" (0.03 vs expected 2.0), but this is **correct** - the SDF models (like Kim, hand_cigarette) are designed at small scales (0.1-0.2 units). The mesh faithfully represents the actual model geometry.

## Testing
- Generated OBJ at 704 resolution: 17,364 vertices, 5,788 triangles
- Rendered in Blender - shows correct hand-with-cigarette geometry
- Face indices now export correctly without commas

## Files Modified
- `vulkan/marching_cubes.hpp` - Added locale fix and include
- `vulkan/sdf_engine.hpp` - Added stored mesh, updated generate_mesh_preview and export functions

## Future Enhancement: Texture Export
Currently exports raw geometry only. To add colors:
1. **Vertex colors** - Sample shader material color at each vertex
2. **UV + texture** - Generate UVs and bake shader colors to texture image
