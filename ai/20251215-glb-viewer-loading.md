# GLB Viewer Loading - 2024-12-15

## Problem
After exporting a mesh to GLB, the user wanted to view it directly in the viewer to compare with the generated mesh and debug any visual differences (like the "blocky" appearance issue seen in Blender).

## Solution
Added GLB loading and display functionality to the viewer.

## Implementation

### 1. GLB Loading Function (marching_cubes.hpp)

Added `mc::loadGLB()` function that uses tinygltf to load GLB files:

```cpp
inline bool loadGLB(const std::string& filename, Mesh& outMesh) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool success = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    // ... parse positions, normals, colors, indices ...
    return !outMesh.vertices.empty();
}
```

Features:
- Loads positions, normals, colors (if present)
- Handles GLTF accessor strides
- Supports uint32, uint16, uint8 index formats
- Merges multiple meshes/primitives into single output mesh

### 2. Display Function (sdf_engine.hpp)

Added `load_glb_and_display()` function:

```cpp
inline bool load_glb_and_display(const char* filepath) {
    mc::Mesh loadedMesh;
    if (!mc::loadGLB(filepath, loadedMesh)) return false;

    e->currentMesh = loadedMesh;
    return upload_mesh_preview(loadedMesh, loadedMesh.colors);
}
```

### 3. UI Button (ui.jank)

Added "View exported_scene.glb" button in the mesh export section:

```clojure
(when (imgui/Button "View exported_scene.glb")
  (if (sdfx/load_glb_and_display "exported_scene.glb")
    (println "Loaded exported_scene.glb into viewer")
    (println "Failed to load exported_scene.glb")))
```

## Files Modified

| File | Changes |
|------|---------|
| `vulkan/marching_cubes.hpp` | Added `loadGLB()` function (~130 lines) |
| `vulkan/sdf_engine.hpp` | Added `load_glb_and_display()` function (~25 lines) |
| `src/vybe/sdf/ui.jank` | Added "View exported_scene.glb" button |

## Testing

Build and run:
```bash
make sdf
```

Output confirms loading works:
```
Loaded GLB: exported_scene.glb - 112792 vertices, 169188 triangles, with colors, with normals
Mesh preview uploaded: 112792 vertices, 169188 triangles
GLB loaded and displayed: exported_scene.glb
```

## Usage

1. Export a mesh using "Export GLB" button
2. Click "View exported_scene.glb" button to reload and view the exported file
3. Compare with the generated preview to debug any visual differences

## Notes

- The loaded GLB mesh replaces the current preview mesh
- Colors and normals are preserved when loading
- Can be used to debug export issues by comparing viewer vs external tools like Blender
