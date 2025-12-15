# Mesh Scale Slider Implementation

## What was done

### Completed Tasks
1. **Solid mesh pipeline with depth testing** - Added depth buffer to Vulkan render pass
2. **UI toggle for wireframe vs solid** - Added "Solid Mode" checkbox
3. **Fixed mesh position/scale to match SDF** - Matched coordinate system (Y-down up vector, focal length perspective)
4. **Added mesh scale slider** - Interactive scale control in ImGui panel

### Key Changes

**mesh.vert** - Vertex shader changes:
- Changed up vector from `vec3(0, 1, 0)` to `vec3(0, -1, 0)` to match SDF raymarcher
- Changed perspective to use `fov` as focal length (not angle) to match SDF
- Added scale factor read from `ubo.resolution.w`
- Applied scale: `vec3 scaledPos = inPosition * scale;`

**ui.jank** - UI additions:
```clojure
;; Scale slider (range 0.1 to 3.0)
(imgui/SliderFloat "Mesh Scale" (cpp/unbox *mesh-scale) (cpp/float. 0.1) (cpp/float. 3.0))
(sdfx/set_mesh_scale (u/p->v *mesh-scale))
```

**sdf_engine.hpp** - Engine changes:
- `meshScale` field in Engine struct
- `get_mesh_scale()` / `set_mesh_scale()` API functions
- Pass scale via UBO: `ubo.resolution[3] = e->meshScale;`

## Commands Run
```bash
glslc mesh.vert -o mesh.vert.spv
```

## Pending Task
- **Add color sampling from SDF shader** - Export mesh with actual shader colors (shadows, lighting) instead of just geometry

## Learnings
- SDF raymarcher uses `fov` as a focal length (Z component of ray direction), not an angle
- SDF coordinate system uses `up = vec3(0, -1, 0)` (Y-down)
- jank `v->p` / `p->v` pattern for ImGui state binding
- Continuous mode fix: only set `dirty = true` when value actually changes
