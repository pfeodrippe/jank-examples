# Viewport Screenshot & nREPL Debugging Workflow

## Overview
This documents how to capture the final rendered viewport (with mesh overlay) and debug shader issues using nREPL.

## Viewport Screenshot Function

### Usage via nREPL
```clojure
(in-ns 'vybe.sdf.ui)
(sdfx/save_viewport_screenshot "viewport_test.png")
```

### Implementation Details
Located in `vulkan/sdf_engine.hpp`:
- Uses `currentImageIndex` field to track current swapchain image
- Creates staging buffer, copies swapchain image (with layout transitions)
- Converts BGRA to RGB with vertical flip
- Saves PNG using stb_image_write

### Why Not compute_screenshot?
`compute_screenshot()` captures the compute shader output (raymarched SDF), not the final frame with mesh overlay. Use `save_viewport_screenshot()` for the complete rendered view.

## nREPL Debugging Workflow for Mesh Colors

### Step 1: Setup
```clojure
(in-ns 'vybe.sdf.ui)
```

### Step 2: Enable mesh with colors
```clojure
(sdfx/set_mesh_preview_visible true)
(sdfx/set_mesh_use_vertex_colors true)
```

### Step 3: Generate mesh (triggers color sampling)
```clojure
(sdfx/generate_mesh_preview (cpp/int. 256))
```

### Step 4: Capture viewport
```clojure
(sdfx/save_viewport_screenshot "test_output.png")
```

### Step 5: Iterate
- Modify `vulkan_kim/color_sampler.comp` (hot-reloads automatically)
- Re-run step 3 to regenerate mesh with new colors
- Re-run step 4 to capture result

## Hot-Reload for color_sampler.comp

The color sampler hot-reloads when EITHER file changes:
- `vulkan_kim/color_sampler.comp` (template)
- `vulkan_kim/<scene>.comp` (scene shader)

Implementation in `init_color_sampler()` checks both modification times.

## Debugging Blue Mesh (6-Direction Shadows)

Systematic approach used to isolate the issue:

1. **Test albedo only**: `vec3 col = albedo;` → skin tones ✓
2. **Test diffuse**: `vec3 col = albedo * (diff * 0.7 + 0.3);` → skin tones ✓
3. **Test diffuse + AO**: Add `* ao` → skin tones ✓
4. **Test single shadow**: Add one calcSoftShadow → identify if shadows cause issue
5. **Test 6-dir loop**: Full implementation → blue mesh (issue found here)

The issue is in the calcSoftShadow function when called multiple times in a loop.

## Common Issues

### Mesh not showing colors
- Check `mc::computeNormals()` is called before `sample_vertex_colors()`
- Console should show "Sampled N vertex colors"
- Check "Include Colors" checkbox is checked in UI

### Screenshot is black
- Make sure mesh is visible and generated
- Check swapchain image layout transitions

### Hot-reload not working
- Both template AND scene shader mod times are checked
- Verify files are being saved (not just edited in memory)

### Blue/cyan mesh (shadow self-intersection)
The most common cause is shadow rays starting ON the surface and immediately hitting it.

**Fix:** Offset shadow ray origin along normal:
```glsl
vec3 shadowOrigin = p + n * 0.01;
float shadow = calcSoftShadow(shadowOrigin, lightDir, 0.02, 10.0, 8.0);
```

### Near-black colors ignored (cyan fallback)
mesh.frag checks `fragColor.r > 0.001 || fragColor.g > 0.001 || fragColor.b > 0.001`. 
If all color channels are near-zero, it falls back to cyan.

**Fix:** Add minimum lighting floor:
```glsl
float lighting = max((totalLight * 0.7 + 0.3) * ao, 0.15);
```

## Key Files
- `vulkan_kim/color_sampler.comp` - GPU color sampling shader
- `vulkan_kim/mesh.frag` - Mesh rendering shader (cyan fallback here)
- `vulkan/sdf_engine.hpp` - Color sampler initialization and screenshot functions
- `src/vybe/sdf/ui.jank` - UI state (*export-colors pointer)