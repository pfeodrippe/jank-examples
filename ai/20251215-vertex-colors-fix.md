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

## Shadow Colors Fix (same session)

### Issue: Mesh shadow areas lacked colorful variations
The raymarched view showed rich colorful shadows (warm reds, cool blues/greens) while the mesh preview had flat/bland tan shadows despite colors being sampled.

### Root Cause 1: Faceted normals from marching cubes
Marching cubes produces faceted mesh normals that create harsh, banded shadows instead of smooth shading.

### Fix 1: Use SDF gradient normals instead of mesh normals
Added `calcNormal(p)` to compute smooth normals from the SDF gradient in `color_sampler.comp`:

```glsl
// Compute smooth normal from SDF gradient (matches raymarched view)
vec3 calcNormal(vec3 p) {
    const float eps = 0.0001;
    vec2 e = vec2(eps, 0);
    return normalize(vec3(
        sceneSDF(p + e.xyy) - sceneSDF(p - e.xyy),
        sceneSDF(p + e.yxy) - sceneSDF(p - e.yxy),
        sceneSDF(p + e.yyx) - sceneSDF(p - e.yyx)
    ));
}
```

Changed main() to use this instead of mesh normal:
```glsl
vec3 n = calcNormal(p);  // Instead of normals[idx].xyz
```

### Root Cause 2: Incorrect UV scale for painterly noise
The painterly color variations use noise functions with UV coordinates. Main shader uses screen UV (0-1 range), but color_sampler used position-based UV with wrong scaling:

- Main shader: `noise2D(uv * 180.0)` where uv ∈ [0, 1]
- Color sampler (broken): `uv = p.xy * 100.0` → uv ∈ [-200, 200], then `uv * 1.8` → still huge values

This created either too-low-frequency noise (1.8 scale) or way-too-high-frequency (180 scale with position-based UV).

### Fix 2: Normalize position to screen-like UV range
Changed UV calculation to normalize position coordinates:

```glsl
// Before (broken)
vec2 uv = p.xy * 100.0 + p.zx * 50.0;
float colorVar = noise2D(uv * 1.8 + p.xy * 20.0);

// After (fixed)
vec2 uv = (p.xy + 2.0) / 4.0 + (p.zx + 1.0) / 4.0 * 0.5;  // Normalize to [0, ~1.5]
float colorVar = noise2D(uv * 180.0 + p.xy * 20.0);  // Now matches main shader
```

Result: Mesh preview shadow areas now show similar colorful warm/cool variations as raymarched view.

### Key Files Modified
- `vulkan_kim/color_sampler.comp` - Added calcNormal(), normalized UV calculation, correct noise scales

## Viewport Screenshot Function (same session)

Added `save_viewport_screenshot()` function to capture the final rendered frame (with mesh overlay) instead of just the compute shader output.

### Key Changes
- Added `currentImageIndex` field to Engine struct to track current swapchain image
- Created `save_viewport_screenshot(filepath)` function that:
  - Creates staging buffer
  - Copies swapchain image to buffer (with layout transitions)
  - Converts BGRA to RGB with vertical flip
  - Saves PNG using stb_image_write

### Usage via nREPL
```clojure
(in-ns 'vybe.sdf.ui)
(sdfx/save_viewport_screenshot "viewport.png")
```

### Files Modified
- `vulkan/sdf_engine.hpp` - Added `currentImageIndex`, `save_viewport_screenshot()`

## 6-Direction Shadow Sampling (same session)

Implemented view-independent shadow baking using 6 light directions for consistent mesh appearance when rotated.

### CRITICAL FIX: Shadow Ray Origin Offset

The initial 6-direction implementation caused blue mesh because shadow rays started ON the surface, immediately hitting it and returning 0.0 (full shadow).

**The Fix:** Offset the shadow ray origin along the surface normal:
```glsl
// Offset shadow ray origin to avoid self-intersection
vec3 shadowOrigin = p + n * 0.01;
float shadow = calcSoftShadow(shadowOrigin, lightDir, 0.02, 10.0, 8.0);
```

### Key Changes to color_sampler.comp
```glsl
// 6-direction shadow sampling for view-independent baking
vec3 lightDirs[6] = vec3[6](
    normalize(vec3(0.0, 1.0, 0.3)),   // Top-front
    normalize(vec3(0.0, 1.0, -0.3)),  // Top-back
    normalize(vec3(1.0, 0.5, 0.0)),   // Right
    normalize(vec3(-1.0, 0.5, 0.0)),  // Left
    normalize(vec3(0.0, 0.3, 1.0)),   // Front
    normalize(vec3(0.0, 0.3, -1.0))   // Back
);
float lightWeights[6] = float[6](0.3, 0.2, 0.15, 0.15, 0.1, 0.1);

// Offset shadow ray origin to avoid self-intersection
vec3 shadowOrigin = p + n * 0.01;

// Accumulate weighted lighting from all 6 directions
float totalLight = 0.0;
for (int i = 0; i < 6; i++) {
    float diff = max(dot(n, lightDirs[i]), 0.0);
    float shadow = calcSoftShadow(shadowOrigin, lightDirs[i], 0.02, 10.0, 8.0);
    totalLight += lightWeights[i] * diff * shadow;
}

// Ensure minimum lighting to prevent near-black colors
float lighting = max((totalLight * 0.7 + 0.3) * ao, 0.15);
```

- Weighted shadow averaging from 6 directions
- Shadow ray origin offset prevents self-intersection
- Minimum lighting floor (0.15) prevents near-black colors that get ignored
- No view-dependent effects (specular, rim light removed)
- AO still applied for depth cues

## Hot Reload Fix for color_sampler.comp

Fixed bug where changes to `color_sampler.comp` weren't being hot-reloaded because only the scene shader modification time was checked.

### Fix in `init_color_sampler()`
```cpp
// Check both scene shader AND template modification times
std::string templatePath = e->shaderDir + "/color_sampler.comp";
time_t templateModTime = 0;
if (stat(templatePath.c_str(), &st) == 0) {
    templateModTime = st.st_mtime;
}
// Use max of both mod times
time_t currentModTime = sceneModTime > templateModTime ? sceneModTime : templateModTime;
```

## Painterly Effects Removed from Main Shader

User requested keeping brush strokes but removing RGB noise from hand_cigarette.comp:

```glsl
// Painterly effects (brush + edge darkening, no RGB noise)
if (matID != MAT_EMBER) {
    float brush = brushStroke(p.xy * 60.0 + p.zx * 30.0, n, 50.0);
    col *= (0.85 + brush * 0.3);

    float edgeFactor = pow(1.0 - abs(dot(n, rd)), 0.7);
    col *= (1.0 - edgeFactor * 0.25);
}
```

Removed: posterization, RGB color noise variations

## Default "Include Colors" Setting (continued session)

Changed default for "Include Colors" checkbox from false to true:

### Files Modified
- `src/vybe/sdf/ui.jank` line 15: `(defonce *export-colors (u/v->p true))`
- `vulkan/sdf_engine.hpp` line 237: `bool meshUseVertexColors = true;`

### Why UI Overrides Engine Flag
The UI calls `set_mesh_use_vertex_colors` every frame with the checkbox value, so setting the engine flag alone doesn't persist. The UI state must be set via the pointer.

## Summary of All Fixes

1. **Shadow ray origin offset** - Critical fix for calcSoftShadow, offset by `p + n * 0.01`
2. **Minimum lighting floor** - `max(lighting, 0.15)` prevents near-black colors that get ignored
3. **Default Include Colors = true** - Both UI and engine defaults changed
4. **Hot reload fix** - Both template and scene shader mod times checked
5. **6-direction shadow sampling** - View-independent baked lighting
