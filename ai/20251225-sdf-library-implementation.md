# SDF Library Implementation - Session Summary

**Date:** 2025-12-25
**Task:** Create reusable SDF patterns library and refactor hand_cigarette.comp

---

## What Was Done

### 1. Extensive Research
- Studied Inigo Quilez's complete SDF articles (3D, 2D, smin, domain repetition, FBM)
- Analyzed hg_sdf library architecture
- Reviewed SDF Modeler for modifier patterns
- Investigated animation/morphing techniques

### 2. Created Comprehensive Library: `vulkan_kim/sdf_lib.glsl`
A 900+ line library with 14 sections:

| Section | Contents |
|---------|----------|
| Helpers | dot2, ndot, length alternatives |
| Rotation | sdfRotateX/Y/Z, sdfRotate2D |
| 2D Primitives | 25+ shapes (circle, box, star, heart, moon, bezier, etc.) |
| 3D Primitives | 35+ shapes (sphere, box, torus, capsule, etc.) |
| Boolean Ops | union, subtract, intersect, xor |
| Smooth Blending | 8 smin variants (quadratic, cubic, circular, etc.) + material blending |
| Domain Transforms | elongate, round, onion/hollow, scale |
| Domain Repetition | infinite, limited, polar, symmetry |
| Deformations | twist, bend, taper, displacement |
| 2D→3D Ops | extrude, revolution |
| Noise | hash, value noise, FBM (2D/3D) |
| Organic Detail | FBM-based surface detail |
| Lighting | soft shadow, ambient occlusion, subsurface scattering |
| Animation | morph, pulse, wave, breathe |

### 3. Refactored hand_cigarette.comp
- Added `#include "sdf_lib.glsl"` with proper extension directive
- Created backward-compatible aliases (rotateX → sdfRotateX, etc.)
- Replaced inline SDF primitives and operations with library versions
- **Preserved exact original shape** - no visual changes from refactoring

---

## Key Learnings

### CRITICAL: Refactoring Must Preserve Exact Formulas!
When refactoring SDF code into a library, you MUST use the EXACT same formulas. Different but "equivalent" formulas produce visibly different shapes!

**Example - opSmoothUnion:**
```glsl
// ORIGINAL (must keep this exact formula!)
float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
return mix(d2, d1, h) - k * h * (1.0 - h);

// WRONG - IQ's newer "normalized" version (looks different!)
k *= 4.0;
float h = max(k - abs(d1 - d2), 0.0) / k;
return min(d1, d2) - h * h * k * 0.25;
```

**Example - sdRoundBox:**
```glsl
// ORIGINAL (simple, matches existing code)
return sdBox(p, b) - r;

// WRONG - IQ's "exact" version (mathematically better but different!)
vec3 q = abs(p) - b + r;
return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
```

### Exact vs Bound SDFs
- **Exact**: Gradient magnitude = 1 always (best quality)
- **Bound**: Lower bound to distance (safe but less accurate)
- Some ops (smooth min, ellipsoid) can NEVER be exact mathematically

### Smooth Minimum Comparison
| Variant | Speed | Quality | Use Case |
|---------|-------|---------|----------|
| Quadratic | Fast | Good | Default choice |
| Cubic | Medium | Better | Smoother blends |
| Circular | Slow | Perfect | Precise circular profiles |

### Domain Repetition Performance
```glsl
// O(1) infinite repetition - no loops!
vec3 q = p - spacing * round(p / spacing);
return primitive(q);
```

### FBM in SDFs - Critical Insight
> "The (regular, arithmetic) addition of two SDFs is not an SDF."

**Solution**: Use smooth intersection to clip noise layers to surface vicinity, then smooth union to merge:
```glsl
n = smax(n, baseD - 0.1 * scale, 0.3 * scale);  // Clip
d = smin(n, d, 0.3 * scale);                     // Merge
```

### Library Design Patterns
1. **Guard headers**: `#ifndef SDF_LIB_GLSL`
2. **Prefixed names**: `sdfRotateX` to avoid collisions
3. **Material variants**: Return `vec2(distance, blendFactor)`
4. **Modular sections**: Easy to find and extend

---

## Commands Used

```bash
# Research (via Claude web tools)
- Fetched iquilezles.org/articles/distfunctions/
- Fetched iquilezles.org/articles/smin/
- Fetched iquilezles.org/articles/sdfrepetition/
- Fetched iquilezles.org/articles/fbmsdf/
- Fetched mercury.sexy/hg_sdf/
- Searched for SDF patterns 2024/2025
```

---

## Files Created/Modified

### Created
- `vulkan_kim/sdf_lib.glsl` - Comprehensive SDF library (900+ lines)
- `ai/20251225-sdf-patterns-library-investigation.md` - Detailed implementation plan
- `ai/20251225-sdf-library-implementation.md` - This file

### Modified
- `vulkan_kim/hand_cigarette.comp` - Refactored to use library with `#include`
- `vulkan/sdf_engine.hpp` - Added `FileIncluder` class for shaderc include support
- `Makefile` - Added `-I vulkan_kim` flag to GLSLC_FLAGS

## Critical Discovery: Shader Includes in shaderc

The runtime shader compiler (`shaderc`) needed a custom `IncluderInterface` implementation to support `#include` directives.

### Key Changes to sdf_engine.hpp

Added `FileIncluder` class that:
1. Implements `shaderc::CompileOptions::IncluderInterface`
2. Resolves relative includes from the requesting file's directory
3. Falls back to the shader base directory (`vulkan_kim/`)
4. Properly manages memory for include results

```cpp
class FileIncluder : public shaderc::CompileOptions::IncluderInterface {
    // GetInclude() - reads file and returns contents
    // ReleaseInclude() - frees allocated memory
};

// Usage:
options.SetIncluder(std::make_unique<FileIncluder>(include_dir));
```

### Shader Include Syntax

```glsl
#extension GL_GOOGLE_include_directive : enable
#include "sdf_lib.glsl"
```

**Sources:**
- [shaderc GitHub](https://github.com/google/shaderc)
- [GL_GOOGLE_include_directive](https://github.com/KhronosGroup/glslang/issues/249)

---

## What's Next

### Potential Enhancements
1. Add more 2D primitives for extrusion (gear, arrow, speech bubble)
2. Implement bezier spline extrusion for organic curves
3. Add material ID support to smooth operations
4. Create GLSL include mechanism if not already supported by shader compiler
5. Add Voronoi patterns for cellular textures
6. Implement subsurface scattering for skin rendering

### Status: REVERTED / NOT COMPLETE
The refactoring was attempted but had issues and was reverted by the user. The changes to `hand_cigarette.comp`, `sdf_engine.hpp`, and `Makefile` have been undone.

### What Needs To Be Done
1. Re-attempt the refactoring with a more careful approach
2. Ensure the `#include` mechanism works correctly with shaderc
3. Preserve the EXACT original formulas to avoid shape changes
4. Test thoroughly before considering complete

---

## References

- [Inigo Quilez 3D SDFs](https://iquilezles.org/articles/distfunctions/)
- [Inigo Quilez 2D SDFs](https://iquilezles.org/articles/distfunctions2d/)
- [Smooth Minimum](https://iquilezles.org/articles/smin/)
- [Domain Repetition](https://iquilezles.org/articles/sdfrepetition/)
- [FBM in SDFs](https://iquilezles.org/articles/fbmsdf/)
- [hg_sdf Library](https://mercury.sexy/hg_sdf/)
- [SDF Modeler](https://sascha-rode.itch.io/sdf-modeler)
