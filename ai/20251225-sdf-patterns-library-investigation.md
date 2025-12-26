# SDF Patterns Library Investigation & Implementation Plan

**Date:** 2025-12-25
**Goal:** Create a comprehensive, reusable SDF (Signed Distance Functions) library for the vulkan_kim shaders

---

## Executive Summary

This document details the investigation into SDF patterns from multiple authoritative sources, including Inigo Quilez's articles, hg_sdf library, SDF Modeler, and academic research. The goal is to create a modular `sdf_lib.glsl` that can be `#include`d into any compute shader for consistent, high-quality SDF modeling.

---

## Research Sources

### Primary Sources
1. **[Inigo Quilez - 3D SDF Functions](https://iquilezles.org/articles/distfunctions/)** - The definitive reference for SDF primitives
2. **[Inigo Quilez - 2D SDF Functions](https://iquilezles.org/articles/distfunctions2d/)** - 2D primitives (useful for extrusion/revolution)
3. **[Inigo Quilez - Smooth Minimum](https://iquilezles.org/articles/smin/)** - Comprehensive smooth blending functions
4. **[Inigo Quilez - Domain Repetition](https://iquilezles.org/articles/sdfrepetition/)** - Infinite/finite repetition patterns
5. **[Inigo Quilez - FBM in SDFs](https://iquilezles.org/articles/fbmsdf/)** - Fractal detail for organic shapes
6. **[hg_sdf Library](https://mercury.sexy/hg_sdf/)** - Production-grade SDF toolkit
7. **[SDF Modeler](https://sascha-rode.itch.io/sdf-modeler)** - Reference for modifiers and boolean ops

### Key Insights from Research

#### Exact vs Bound SDFs
- **Exact SDF**: Distance gradient always has unit length (|∇d| = 1)
- **Bound SDF**: Returns lower bound to real distance (safe for raymarching but less accurate)
- Some primitives (ellipsoid) and operators (smooth min) cannot be exact mathematically
- Prefer exact SDFs when possible for better quality

#### Domain Repetition Performance
> "Even straightforwardly doing patterns with unions would be an improvement with SDFs because it is linear in the number of copies instead of quadratic, and you can potentially do it in constant time with domain repetition."

#### Lipschitz Continuity
- Critical for raymarching: the SDF gradient magnitude should not exceed 1
- Violated by many deformations (twist, bend) - use conservative step sizes

---

## Library Architecture

### File Structure
```
vulkan_kim/
├── sdf_lib.glsl           # Main library (all functions)
├── hand_cigarette.comp    # Uses #include "sdf_lib.glsl"
└── sdf_scene.comp         # Other shaders can include too
```

### Module Organization (within sdf_lib.glsl)

```glsl
// ============================================================================
// SECTION 1: HELPER FUNCTIONS
// ============================================================================
// - dot2(v), ndot(a,b), length alternatives (length2, length6, length8)
// - hash functions for procedural generation

// ============================================================================
// SECTION 2: 2D SDF PRIMITIVES
// ============================================================================
// - Circle, Box, Triangle, Polygons, Star, Heart, etc.
// - Useful for extrusion and revolution into 3D

// ============================================================================
// SECTION 3: 3D SDF PRIMITIVES
// ============================================================================
// - Sphere, Box, Torus, Cylinder, Capsule, Cone, etc.
// - All exact or clearly marked as bound

// ============================================================================
// SECTION 4: BOOLEAN OPERATIONS
// ============================================================================
// - Union, Subtraction, Intersection, XOR
// - Hard and Smooth variants

// ============================================================================
// SECTION 5: SMOOTH BLENDING (smin/smax family)
// ============================================================================
// - Polynomial (quadratic, cubic, quartic)
// - Exponential, Root, Circular
// - Material blend variants

// ============================================================================
// SECTION 6: DOMAIN TRANSFORMATIONS
// ============================================================================
// - Translation, Rotation, Scale
// - Elongation, Rounding, Onion/Hollowing

// ============================================================================
// SECTION 7: DOMAIN REPETITION
// ============================================================================
// - Infinite repetition (1D, 2D, 3D)
// - Limited/finite repetition
// - Radial/angular repetition
// - Mirrored repetition

// ============================================================================
// SECTION 8: DEFORMATIONS
// ============================================================================
// - Twist, Bend, Taper
// - Displacement (noise-based)

// ============================================================================
// SECTION 9: 2D→3D OPERATIONS
// ============================================================================
// - Extrusion
// - Revolution

// ============================================================================
// SECTION 10: NOISE & PROCEDURAL
// ============================================================================
// - Hash functions (1D, 2D, 3D)
// - Value noise, Gradient noise
// - FBM (fractal brownian motion)
// - Organic detail displacement

// ============================================================================
// SECTION 11: LIGHTING UTILITIES
// ============================================================================
// - Soft shadows
// - Ambient occlusion
// - Subsurface scattering approximation
```

---

## Detailed Function Specifications

### Section 4: Boolean Operations

| Function | Type | Description |
|----------|------|-------------|
| `opUnion(d1, d2)` | Exact | `min(d1, d2)` |
| `opSubtract(d1, d2)` | Bound | `max(-d1, d2)` |
| `opIntersect(d1, d2)` | Bound | `max(d1, d2)` |
| `opXor(d1, d2)` | Exact | `max(min(d1,d2), -max(d1,d2))` |

### Section 5: Smooth Blending

**Recommended Default: Quadratic Polynomial**
```glsl
float opSmoothUnion(float d1, float d2, float k) {
    k *= 4.0;
    float h = max(k - abs(d1 - d2), 0.0) / k;
    return min(d1, d2) - h * h * k * 0.25;
}
```
- Fast, rigid, conservative
- `k` = blend radius in world units

**For Smoother Transitions: Cubic**
```glsl
float opSmoothUnionCubic(float d1, float d2, float k) {
    k *= 6.0;
    float h = max(k - abs(d1 - d2), 0.0) / k;
    return min(d1, d2) - h * h * h * k * (1.0/6.0);
}
```

**For Perfect Circular Blend:**
```glsl
float opSmoothUnionCircular(float d1, float d2, float k) {
    k *= 1.0 / (1.0 - sqrt(0.5));
    float h = max(k - abs(d1 - d2), 0.0) / k;
    return min(d1, d2) - k * 0.5 * (1.0 + h - sqrt(1.0 - h * (h - 2.0)));
}
```

**Material Blending (returns distance + blend factor):**
```glsl
vec2 opSmoothUnionMaterial(float d1, float d2, float k) {
    float h = 1.0 - min(abs(d1 - d2) / (4.0 * k), 1.0);
    float w = h * h;
    float m = w * 0.5;
    float s = w * k;
    return (d1 < d2) ? vec2(d1 - s, m) : vec2(d2 - s, 1.0 - m);
}
```

### Section 7: Domain Repetition

**Infinite Repetition (Simple, symmetric shapes only)**
```glsl
vec3 opRepeat(vec3 p, vec3 spacing) {
    return p - spacing * round(p / spacing);
}
```

**Limited Repetition**
```glsl
vec3 opRepeatLimited(vec3 p, float spacing, vec3 limit) {
    return p - spacing * clamp(round(p / spacing), -limit, limit);
}
```

**Radial/Angular Repetition**
```glsl
vec2 opRepeatPolar(vec2 p, int n) {
    float angle = 6.283185 / float(n);
    float a = atan(p.y, p.x) + angle * 0.5;
    a = mod(a, angle) - angle * 0.5;
    return length(p) * vec2(cos(a), sin(a));
}
```

### Section 8: Deformations

**Twist (around Y axis)**
```glsl
vec3 opTwist(vec3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    mat2 m = mat2(c, -s, s, c);
    return vec3(m * p.xz, p.y);
}
```

**Bend (around X axis)**
```glsl
vec3 opBend(vec3 p, float k) {
    float c = cos(k * p.x);
    float s = sin(k * p.x);
    mat2 m = mat2(c, -s, s, c);
    return vec3(m * p.xy, p.z);
}
```

**Displacement**
```glsl
float opDisplace(float d, vec3 p, float amplitude, float frequency) {
    float disp = sin(p.x * frequency) * sin(p.y * frequency) * sin(p.z * frequency);
    return d + disp * amplitude;
}
```

### Section 9: 2D→3D Operations

**Extrusion**
```glsl
float opExtrude(vec3 p, float d2d, float h) {
    vec2 w = vec2(d2d, abs(p.z) - h);
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}
```

**Revolution**
```glsl
float opRevolve(vec3 p, float offset) {
    vec2 q = vec2(length(p.xz) - offset, p.y);
    return sd2D(q);  // Apply 2D SDF to q
}
```

---

## Animation Patterns

### Morphing Between Shapes
```glsl
float sdMorph(vec3 p, float t) {
    float d1 = sdShape1(p);
    float d2 = sdShape2(p);
    return mix(d1, d2, smoothstep(0.0, 1.0, t));
}
```

### Time-based Displacement
```glsl
float sdAnimatedDisplace(vec3 p, float d, float time) {
    float wave = sin(p.x * 10.0 + time) * sin(p.y * 10.0 + time * 0.7);
    return d + wave * 0.02;
}
```

### Breathing/Pulsing
```glsl
float sdPulsing(vec3 p, float radius, float time) {
    float pulse = 1.0 + sin(time * 2.0) * 0.1;
    return sdSphere(p, radius * pulse);
}
```

---

## Organic Detail with FBM

### Key Insight from Inigo Quilez
> "The (regular, arithmetic) addition of two SDFs is not an SDF."

### Solution: Layered SDF Octaves
```glsl
float sdWithFBM(vec3 p, float baseD) {
    float d = baseD;
    float scale = 1.0;

    for (int i = 0; i < 4; i++) {
        // Create noise layer
        float n = scale * sdNoiseBase(p);

        // Clip to surface vicinity (smooth intersection)
        n = smax(n, d - 0.1 * scale, 0.3 * scale);

        // Merge with smooth union
        d = smin(n, d, 0.3 * scale);

        // Double frequency, halve amplitude
        p = rotationMatrix * p;
        scale *= 0.5;
    }
    return d;
}
```

---

## Implementation Plan

### Phase 1: Create sdf_lib.glsl
1. Implement all helper functions
2. Port 2D primitives (for extrusion)
3. Port 3D primitives
4. Implement boolean operations
5. Implement smooth blending (all variants)
6. Add domain operations
7. Add deformations
8. Add noise/procedural functions

### Phase 2: Refactor hand_cigarette.comp
1. Replace inline primitives with library calls
2. Add `#include "sdf_lib.glsl"` at top
3. Simplify existing code
4. Add new organic detail to skin using FBM
5. Add more realistic smoke using domain distortion

### Phase 3: Testing & Optimization
1. Verify visual output matches original
2. Profile performance
3. Add LOD for noise functions

---

## Key Patterns for hand_cigarette.comp

### Current Patterns (Already Implemented)
- `sdSphere`, `sdBox`, `sdRoundBox`, `sdEllipsoid`, `sdCapsule`
- `sdCylinder`, `sdCone`, `sdTorus`
- `opUnion`, `opSubtract`, `opIntersect`
- `opSmoothUnion`, `opSmoothSubtract`
- `rotateX`, `rotateY`, `rotateZ`

### New Patterns to Add
1. **Smooth Intersection** - for carving details
2. **Domain Repetition** - for procedural patterns
3. **Twist/Bend** - for organic finger deformation
4. **FBM Displacement** - for skin texture
5. **Cubic/Quartic smooth ops** - for smoother blends
6. **Material blending** - for proper transitions

### Potential Enhancements
- Add subtle skin wrinkles using displacement
- Make smoke more volumetric with fbm-based density
- Add dirt/texture under fingernails
- Cigarette paper texture with subtle noise

---

## Performance Considerations

### Do's
- Use domain repetition for infinite patterns (O(1))
- Early-exit noise loops based on pixel size
- Prefer exact SDFs when available
- Use symmetry (`abs(p.x)`) to halve computation

### Don'ts
- Don't use loops inside SDF functions (use repetition instead)
- Don't add too many fbm octaves (4-6 max)
- Don't use non-uniform scale with rotations (causes shearing)

---

## References

1. Inigo Quilez Articles: https://iquilezles.org/articles/
2. hg_sdf Library: https://mercury.sexy/hg_sdf/
3. SDF Modeler: https://sascha-rode.itch.io/sdf-modeler
4. The Book of Shaders: https://thebookofshaders.com/
5. Shadertoy Examples: https://www.shadertoy.com/

---

## Next Steps

1. Create `vulkan_kim/sdf_lib.glsl` with all patterns
2. Test include mechanism works with compute shaders
3. Refactor `hand_cigarette.comp` to use library
4. Add new visual enhancements using library patterns
5. Document learnings in `ai/` folder
