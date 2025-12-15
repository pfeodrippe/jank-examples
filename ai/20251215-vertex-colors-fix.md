# Vertex Colors Fix - 2024-12-15

## What Was Fixed

### Issue: Color sampling returned empty results
The mesh preview vertex colors weren't being sampled even when `meshUseVertexColors` was enabled.

### Root Cause: Missing normals computation
The `sample_vertex_colors` function in `sdf_engine.hpp` requires normals to compute proper lighting/colors:
```cpp
if (mesh.normals.size() != numPoints) {
    std::cerr << "Error: mesh normals not computed" << std::endl;
    return {};
}
```

However, `mc::generateMesh` does NOT call `computeNormals` automatically.

### Fix Applied
Added `mc::computeNormals(e->currentMesh);` before calling `sample_vertex_colors` in `generate_mesh_preview`:

```cpp
// Sample vertex colors if enabled
std::vector<mc::Color3> colors;
if (e->meshUseVertexColors) {
    std::cout << "Sampling vertex colors..." << std::endl;
    // Compute normals (required for color sampling)
    mc::computeNormals(e->currentMesh);
    colors = sample_vertex_colors(e->currentMesh);
    ...
}
```

Location: `vulkan/sdf_engine.hpp:3950-3953` (approximately)

## nREPL Interaction Notes

### Setting UI checkbox state from nREPL
The UI calls `set_mesh_use_vertex_colors` every frame with the checkbox value. To override via nREPL:

```clojure
;; DON'T: This doesn't work - jank boolean conversion issue
(sdfx/set_mesh_use_vertex_colors (cpp/bool. true))

;; DO: Set the UI state pointer directly
(cpp/= (cpp/* (cpp/unbox cpp/bool* ui/*export-colors)) (cpp/bool. true))
```

This writes `true` to the pointer that ImGui's Checkbox reads, so the value persists.

### Verifying state
```clojure
;; Check both UI checkbox and engine flag
(println "export-colors:" (u/p->v ui/*export-colors) "engine flag:" (sdfx/get_mesh_use_vertex_colors))
```

## Commands Run
```bash
make sdf                    # Restart app
clj-nrepl-eval -p 5557 "..." # Execute code via nREPL
```

## Output When Working
```
Sampling vertex colors...
Initializing color sampler for 2016 points...
Color sampler initialized successfully
Sampling colors for 2016 vertices on GPU...
Color sampling completed in 1 ms
Sampled 2016 vertex colors
Mesh preview uploaded: 2016 vertices, 672 triangles
```

## Status: COMPLETE ✓

The vertex colors feature is fully working:
- Mesh preview shows vertex colors when "Include Colors" checkbox is enabled
- Colors are sampled using the GPU compute shader (color_sampler.comp)
- Mesh export (GLB/OBJ) also uses these colors when the checkbox is enabled
- Performance: Color sampling takes ~1ms for 2016 vertices on M3 Max

## Key Files Modified
- `vulkan/sdf_engine.hpp` - Added `mc::computeNormals()` call before color sampling
- `vulkan_kim/mesh.frag` - Fragment shader accepts vertex colors
- `vulkan_kim/mesh.vert` - Vertex shader passes colors to fragment shader

## Verification
Run the app with `make sdf`, enable "Include Colors" checkbox, and regenerate the mesh.
Console output will show "Sampled N vertex colors" when working correctly.

## Standalone Build Fix (same session)

### Issue: `make sdf-standalone` failed with undefined tinygltf symbols
The linker couldn't find tinygltf implementation functions like `WriteGltfSceneToFile`.

### Root Cause
tinygltf is a header-only library. Its implementation (defined via `TINYGLTF_IMPLEMENTATION` in `marching_cubes.hpp`) wasn't being compiled into an object file for standalone builds.

### Fix Applied
1. Created `vulkan/tinygltf_impl.cpp` - a compilation unit that includes `marching_cubes.hpp`
2. Updated `bin/run_sdf.sh` to compile it and add to `OBJ_FILES`
3. Added `#include <cfloat>` to `marching_cubes.hpp` for `FLT_MAX`
4. Updated `Makefile` clean target to include `tinygltf_impl.o`

Result: `make sdf-standalone` now works and creates `SDFViewer.app` (179MB DMG).

### Additional Fix: JIT mode broken after adding tinygltf_impl.o

**Issue:** `make sdf` failed with "Failed to JIT compile native header require"

**Root Cause:** `tinygltf_impl.o` was included in `OBJ_FILES` which is used for both JIT and standalone builds. In JIT mode, the tinygltf implementation comes from JIT-compiling `marching_cubes.hpp`, causing duplicate symbols.

**Fix:** Split object files into two arrays:
- `OBJ_FILES` - used for both JIT and standalone
- `STANDALONE_OBJ_FILES` - only used for standalone builds (contains `tinygltf_impl.o`)

Result: Both `make sdf` (JIT) and `make sdf-standalone` (AOT) now work correctly.
