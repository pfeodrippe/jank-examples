# Shader Guide - SDF Viewer Project

## Overview

This project uses a sophisticated shader system for SDF (Signed Distance Function) rendering and mesh generation. The shaders work together in multiple pipelines:

1. **Raymarching Pipeline** - Real-time SDF visualization
2. **Mesh Generation Pipeline** - GPU-accelerated mesh export via Dual Contouring
3. **Display Pipeline** - Final rendering to screen

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           SHADER ARCHITECTURE                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────┐      ┌──────────────────┐      ┌──────────────────┐  │
│  │  RAYMARCHING     │      │  MESH GENERATION │      │  DISPLAY         │  │
│  │  (Live Preview)  │      │  (Export)        │      │  (Final Output)  │  │
│  ├──────────────────┤      ├──────────────────┤      ├──────────────────┤  │
│  │ hand_cigarette   │      │ sdf_sampler      │      │ blit.vert/frag   │  │
│  │ sdf_scene        │      │ dc_mark_active   │      │ mesh.vert/frag   │  │
│  │ (.comp)          │      │ dc_vertices      │      │                  │  │
│  │                  │      │ dc_quads         │      │                  │  │
│  │                  │      │ dc_cubes         │      │                  │  │
│  │                  │      │ color_sampler    │      │                  │  │
│  └──────────────────┘      └──────────────────┘      └──────────────────┘  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Shader Categories

### 1. Scene Shaders (Raymarching)

These are the main SDF scene definition shaders that define what objects exist and how they look.

#### `hand_cigarette.comp`
**Purpose:** Defines a hand holding a cigarette with smoke effects
**Location:** `vulkan_kim/hand_cigarette.comp`

```glsl
layout(local_size_x = 16, local_size_y = 16) in;
layout(binding = 0, rgba8) uniform writeonly image2D outputImage;
layout(std140, binding = 1) uniform UBO { ... };
```

**Key Functions:**
- `sceneSDF(vec3 p)` - Main SDF combining all scene elements
- `sceneSDF_mat(vec3 p)` - Returns `vec2(distance, materialID)`
- `getMaterialColor(int matID, vec3 p)` - Material color lookup
- `render(vec3 ro, vec3 rd)` - Full raymarching + lighting pipeline

**Materials:** `MAT_SKIN`, `MAT_NAIL`, `MAT_CIGARETTE`, `MAT_FILTER`, `MAT_EMBER`, `MAT_SMOKE`, `MAT_GROUND`

**SDF Primitives:**
- `sdSphere`, `sdBox`, `sdRoundBox`, `sdEllipsoid`
- `sdCapsule`, `sdCylinder`, `sdCone`, `sdTorus`
- `opUnion`, `opSubtract`, `opIntersect`
- `opSmoothUnion`, `opSmoothSubtract`

**Features:**
- Animated smoke with dissolving chunks
- Finger animation (fidget, breathing)
- Glowing ember with pulsing effect
- Painterly brush stroke effects

---

#### `sdf_scene.comp`
**Purpose:** Full character model (Kim Kitsuragi from Disco Elysium)
**Location:** `vulkan_kim/sdf_scene.comp`

**Structure:** Same as `hand_cigarette.comp` but defines a full body:
- Head with elongated face, glasses, hair
- Body with jacket, pants, shoes
- Accessories (watch, badge)

**Materials:** `MAT_GROUND`, `MAT_SKIN`, `MAT_JACKET`, `MAT_PANTS`, `MAT_HAIR`, `MAT_GLASSES`, `MAT_SHOES`, `MAT_WATCH`, `MAT_SHIRT`, `MAT_BADGE`, `MAT_EYES`, `MAT_LIPS`

---

### 2. SDF Sampling Shaders

These shaders sample the SDF at grid points for mesh generation.

#### `sdf_sampler.comp`
**Purpose:** Sample SDF values on a 3D grid
**Location:** `vulkan_kim/sdf_sampler.comp`

```glsl
layout(local_size_x = 64) in;

// Output: SDF distances
layout(std430, binding = 0) writeonly buffer OutputDistances { float distances[]; };

// Grid parameters
layout(std140, binding = 1) uniform SamplerParams {
    uint resolution;    // Grid resolution (e.g., 512)
    float time;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};
```

**Code Injection:** Contains markers that get replaced with scene code:
```glsl
// MARKER_SCENE_SDF_START
float sceneSDF(vec3 p) { ... }
// MARKER_SCENE_SDF_END
```

**Memory Optimization:** Computes positions on-the-fly from grid index instead of passing positions buffer (saves 2GB+ for 512³ grids).

**Data Flow:**
```
Grid Parameters → Compute Position → sceneSDF(p) → distances[]
```

---

#### `sdf_sampler_sparse.comp`
**Purpose:** Sparse SDF sampling - only evaluates at specified indices
**Location:** `vulkan_kim/sdf_sampler_sparse.comp`
**Status:** Prepared for future indirect dispatch optimization

```glsl
// Input: list of active cell indices
layout(std430, binding = 2) readonly buffer ActiveIndices { uint indices[]; };

// Only processes indices in the active list
uint idx = indices[workIdx];  // Sparse access!
```

---

#### `sdf_find_active.comp`
**Purpose:** Find coarse cells near the surface for sparse streaming
**Location:** `vulkan_kim/sdf_find_active.comp`
**Status:** Prepared for future GPU-based region detection

```glsl
// Input: coarse grid distances
layout(std430, binding = 0) readonly buffer CoarseDistances { float coarseDistances[]; };

// Output: list of active fine grid indices
layout(std430, binding = 1) buffer ActiveIndices { uint indices[]; };
```

---

### 3. Dual Contouring Shaders

GPU-accelerated mesh generation from SDF volumes.

#### `dc_mark_active.comp`
**Purpose:** Pass 1 - Mark cells containing surface crossings
**Location:** `vulkan_kim/dc_mark_active.comp`

```glsl
// Input: SDF distances
layout(std430, binding = 0) readonly buffer InputDistances { float distances[]; };

// Output: Active cell mask
layout(std430, binding = 1) writeonly buffer ActiveMask { uint activeMask[]; };

// Output: Atomic counter
layout(std430, binding = 2) buffer ActiveCount { uint activeCount; };
```

**Algorithm:**
1. Sample 8 corners of each cell
2. Count corners inside surface (SDF < isolevel)
3. Mark cell active if `0 < insideCount < 8` (sign change)

---

#### `dc_vertices.comp`
**Purpose:** Pass 2 - Compute vertex positions for active cells
**Location:** `vulkan_kim/dc_vertices.comp`

```glsl
// Input: SDF distances, active mask
// Output: Vertex positions, cell-to-vertex mapping

layout(std430, binding = 2) writeonly buffer Vertices { vec4 vertices[]; };
layout(std430, binding = 3) buffer CellToVertex { int cellToVertex[]; };
```

**Algorithm:**
1. Skip inactive cells
2. Find all edge crossings (12 edges per cell)
3. Interpolate crossing positions
4. Compute mass point (simplified QEF)
5. Allocate vertex atomically

---

#### `dc_quads.comp`
**Purpose:** Pass 3 - Generate quad indices connecting cell vertices
**Location:** `vulkan_kim/dc_quads.comp`

```glsl
// Input: SDF distances, cell-to-vertex mapping
// Output: Index buffer (triangles)

layout(std430, binding = 2) writeonly buffer Indices { uint indices[]; };
```

**Algorithm:**
1. Process each edge type (X, Y, Z aligned)
2. Check for sign change on edge
3. Get 4 adjacent cell vertices
4. Write quad as 2 triangles with correct winding

---

#### `dc_cubes.comp`
**Purpose:** Alternative mesh mode - Generate voxel cubes for each active cell
**Location:** `vulkan_kim/dc_cubes.comp`

```glsl
// Output: 8 vertices + 36 indices per active cell
layout(std430, binding = 2) writeonly buffer Vertices { vec4 vertices[]; };
layout(std430, binding = 3) writeonly buffer Indices { uint indices[]; };
```

**Algorithm:**
1. Skip inactive cells
2. Generate 8 cube corner vertices
3. Generate 36 indices (12 triangles, 6 faces)

---

#### `dc_cubes_fused.comp`
**Purpose:** Fused SDF sampling + cube generation (eliminates intermediate buffer)
**Location:** `vulkan_kim/dc_cubes_fused.comp`
**Status:** Prepared for future optimization

```glsl
// No input buffer needed - computes SDF on-the-fly!
float getSDF(uint x, uint y, uint z) {
    vec3 p = gridToWorld(x, y, z);
    return sceneSDF(p);  // Direct evaluation
}
```

---

### 4. Color Sampling Shader

#### `color_sampler.comp`
**Purpose:** Sample surface colors at mesh vertex positions
**Location:** `vulkan_kim/color_sampler.comp`

```glsl
// Input: vertex positions and normals
layout(std430, binding = 0) readonly buffer InputPositions { vec4 positions[]; };
layout(std430, binding = 1) readonly buffer InputNormals { vec4 normals[]; };

// Output: vertex colors
layout(std430, binding = 2) writeonly buffer OutputColors { vec4 colors[]; };
```

**Code Injection:** Contains full scene code including:
- `sceneSDF()`, `sceneSDF_mat()`
- `getMaterialColor()`
- `calcSoftShadow()`, `calcAO()`
- `brushStroke()`, `posterize()` (painterly effects)

**Algorithm:**
1. Compute SDF gradient normal (smoother than mesh normals)
2. Get material ID from `sceneSDF_mat()`
3. Apply full lighting calculation
4. Output gamma-corrected color

---

### 5. Display Shaders

#### `blit.vert` / `blit.frag`
**Purpose:** Display compute shader output to screen
**Location:** `vulkan_kim/blit.vert`, `vulkan_kim/blit.frag`

**Vertex Shader:**
```glsl
// Fullscreen triangle trick - no vertex buffer needed
uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
```

**Fragment Shader:**
```glsl
outColor = texture(tex, uv);  // Simple texture sample
```

---

#### `mesh.vert` / `mesh.frag`
**Purpose:** Render generated mesh preview
**Location:** `vulkan_kim/mesh.vert`, `vulkan_kim/mesh.frag`

**Vertex Shader:**
```glsl
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

// Custom view/projection matching SDF raymarcher
mat4 view = lookAt(eye, target, vec3(0.0, -1.0, 0.0));  // Y-down!
mat4 proj = perspective(fov, aspect, 0.01, 100.0);
```

**Fragment Shader:**
```glsl
// If vertex colors present, pass through directly
if (useVertexColors && fragColor != black) {
    outColor = vec4(fragColor, 1.0);
    return;
}
// Otherwise, simple diffuse lighting
vec3 color = baseColor * (ambient + diff * 0.7);
```

---

## Data Flow Diagrams

### Raymarching Pipeline
```
UBO (camera, light, time)
          │
          ▼
┌─────────────────────────────┐
│  hand_cigarette.comp        │
│  or sdf_scene.comp          │
│                             │
│  1. Ray setup               │
│  2. Raymarch sceneSDF()     │
│  3. Get material + normal   │
│  4. Apply lighting          │
│  5. Painterly effects       │
└─────────────────────────────┘
          │
          ▼
    outputImage (rgba8)
          │
          ▼
┌─────────────────────────────┐
│  blit.vert → blit.frag      │
│  (fullscreen quad)          │
└─────────────────────────────┘
          │
          ▼
      Swapchain
```

### Mesh Generation Pipeline
```
┌─────────────────┐
│ Grid Parameters │
│ (resolution,    │
│  bounds)        │
└────────┬────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────┐
│  STEP 1: SDF SAMPLING                                        │
│  sdf_sampler.comp (with injected scene code)                 │
│                                                              │
│  for each grid point:                                        │
│      p = gridToWorld(ix, iy, iz)                             │
│      distances[idx] = sceneSDF(p)                            │
└─────────────────────────────────────────────────────────────┘
         │
         ▼ distances[]
┌─────────────────────────────────────────────────────────────┐
│  STEP 2: MARK ACTIVE CELLS                                   │
│  dc_mark_active.comp                                         │
│                                                              │
│  for each cell:                                              │
│      sample 8 corners                                        │
│      activeMask[cell] = (signChange ? 1 : 0)                 │
└─────────────────────────────────────────────────────────────┘
         │
         ▼ activeMask[]
┌─────────────────────────────────────────────────────────────┐
│  STEP 3A: GENERATE VERTICES (Standard DC)                    │
│  dc_vertices.comp                                            │
│                                                              │
│  for each active cell:                                       │
│      find edge crossings                                     │
│      vertices[idx] = massPoint / crossingCount               │
└─────────────────────────────────────────────────────────────┘
         │
         ▼ vertices[], cellToVertex[]
┌─────────────────────────────────────────────────────────────┐
│  STEP 3B: GENERATE INDICES (Standard DC)                     │
│  dc_quads.comp                                               │
│                                                              │
│  for each edge with sign change:                             │
│      get 4 adjacent cell vertices                            │
│      write quad as 2 triangles                               │
└─────────────────────────────────────────────────────────────┘
         │
         ▼ indices[]

--- OR (CUBES MODE) ---

┌─────────────────────────────────────────────────────────────┐
│  STEP 3 ALT: GENERATE CUBES                                  │
│  dc_cubes.comp                                               │
│                                                              │
│  for each active cell:                                       │
│      write 8 cube vertices                                   │
│      write 36 cube indices                                   │
└─────────────────────────────────────────────────────────────┘
         │
         ▼ vertices[], indices[]
┌─────────────────────────────────────────────────────────────┐
│  STEP 4: COLOR SAMPLING (Optional)                           │
│  color_sampler.comp (with injected scene code)               │
│                                                              │
│  for each vertex:                                            │
│      n = calcNormal(p)  // SDF gradient                      │
│      matID = sceneSDF_mat(p).y                               │
│      colors[idx] = lighting(getMaterialColor(matID))         │
└─────────────────────────────────────────────────────────────┘
         │
         ▼ colors[]
┌─────────────────────────────────────────────────────────────┐
│  STEP 5: DISPLAY MESH                                        │
│  mesh.vert → mesh.frag                                       │
│                                                              │
│  Transform vertices to clip space                            │
│  Apply vertex colors or default lighting                     │
└─────────────────────────────────────────────────────────────┘
```

---

## Code Injection System

The project uses a code injection system to share SDF scene code between shaders.

### How It Works

1. **Scene Shader** contains the authoritative scene definition:
   ```glsl
   float sceneSDF(vec3 p) { ... }
   vec2 sceneSDF_mat(vec3 p) { ... }
   vec3 getMaterialColor(int matID, vec3 p) { ... }
   ```

2. **Template Shaders** have placeholder markers:
   ```glsl
   // MARKER_SCENE_SDF_START
   float sdSphere(vec3 p, float r) { return length(p) - r; }
   float sceneSDF(vec3 p) { return sdSphere(p, 1.0); }
   // MARKER_SCENE_SDF_END
   ```

3. **At Runtime**, `extract_scene_sdf()` extracts code from scene shader:
   - Finds `sceneSDF` function and all dependencies
   - Recursively extracts helper functions (sdSphere, opUnion, etc.)

4. **Injection** replaces marker region with extracted code:
   ```cpp
   size_t start = templateSrc.find("// MARKER_SCENE_SDF_START");
   size_t end = templateSrc.find("// MARKER_SCENE_SDF_END");
   templateSrc.replace(start, end - start + endMarker.length(), sceneCode);
   ```

### Functions Extracted

For `sdf_sampler.comp`:
- `sceneSDF()` and all SDF primitives
- Boolean operations
- Rotation matrices

For `color_sampler.comp`:
- Everything above, plus:
- `sceneSDF_mat()` for material IDs
- `getMaterialColor()` for material colors
- `calcSoftShadow()`, `calcAO()` for lighting
- `brushStroke()`, `posterize()` for painterly effects

---

## Uniform Buffer Objects (UBO)

### Main Scene UBO
```glsl
layout(std140, binding = 1) uniform UBO {
    vec4 cameraPos;      // xyz = position, w = fov
    vec4 cameraTarget;   // xyz = target
    vec4 lightDir;       // xyz = direction (normalized)
    vec4 resolution;     // xy = width/height, z = time
    vec4 options;        // x = useVertexColors, y/z/w = unused
    vec4 editMode;       // x = enabled, y = selectedObject, z = hoveredAxis, w = objectCount
    vec4 gizmoPos;       // xyz = gizmo position
    vec4 gizmoRot;       // xyz = selected object rotation
    vec4 objPositions[MAX_OBJECTS];
    vec4 objRotations[MAX_OBJECTS];
};
```

### Sampler Params UBO
```glsl
layout(std140, binding = 1) uniform SamplerParams {
    uint resolution;    // Grid resolution
    float time;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};
```

### DC Params UBO
```glsl
layout(std140, binding = X) uniform DCParams {
    uint resolution;
    float isolevel;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    float voxelSize;  // For dc_cubes
};
```

---

## Adding a New Scene Shader

1. **Create new `.comp` file** in `vulkan_kim/`:
   ```glsl
   #version 450
   layout(local_size_x = 16, local_size_y = 16) in;
   layout(binding = 0, rgba8) uniform writeonly image2D outputImage;
   layout(std140, binding = 1) uniform UBO { ... };

   // Define SDF primitives
   float sdSphere(vec3 p, float r) { ... }

   // Define scene
   float sceneSDF(vec3 p) { ... }
   vec2 sceneSDF_mat(vec3 p) { ... }  // Required for colors
   vec3 getMaterialColor(int matID, vec3 p) { ... }  // Required for colors

   // Raymarching + rendering
   void main() { ... }
   ```

2. **Build** - shader will be auto-discovered by file scanner

3. **Switch** using `[` / `]` keys or programmatically

---

## Performance Considerations

### Workgroup Sizes
- Raymarching: `16x16` (256 threads) - matches GPU tile size
- Sampling: `64` (linear dispatch) - good for memory bandwidth
- DC passes: `64` - matches warp/wavefront size

### Memory Bandwidth
- `sdf_sampler.comp` computes positions on-the-fly to avoid 2GB+ positions buffer
- `dc_cubes_fused.comp` eliminates intermediate distances buffer entirely

### Sparse Streaming
For high resolutions (>256), uses sparse streaming:
1. Coarse sample at 1/8 resolution
2. Find surface regions
3. Process only surface regions
4. Result: 99.97% reduction in samples (1B → 300K points)

---

## Shader File Summary

| Shader | Type | Purpose |
|--------|------|---------|
| `hand_cigarette.comp` | Compute | Hand + cigarette scene raymarching |
| `sdf_scene.comp` | Compute | Full character scene raymarching |
| `sdf_sampler.comp` | Compute | SDF grid sampling for mesh gen |
| `sdf_sampler_sparse.comp` | Compute | Sparse SDF sampling (future) |
| `sdf_find_active.comp` | Compute | Find surface regions (future) |
| `dc_mark_active.comp` | Compute | Mark cells with surface crossing |
| `dc_vertices.comp` | Compute | Generate DC vertex positions |
| `dc_quads.comp` | Compute | Generate DC quad indices |
| `dc_cubes.comp` | Compute | Generate voxel cubes |
| `dc_cubes_fused.comp` | Compute | Fused SDF+cubes (future) |
| `color_sampler.comp` | Compute | Sample vertex colors |
| `blit.vert` | Vertex | Fullscreen quad |
| `blit.frag` | Fragment | Texture sampling |
| `mesh.vert` | Vertex | Mesh transform |
| `mesh.frag` | Fragment | Mesh lighting |

---

## Common Patterns

### SDF Distance Functions
```glsl
// Always return SIGNED distance (negative = inside)
float sdPrimitive(vec3 p, ...) {
    return distance_to_surface;  // Can be negative!
}
```

### Boolean Operations
```glsl
// Union: min (closest surface wins)
float opUnion(float d1, float d2) { return min(d1, d2); }

// Subtraction: carve d1 from d2
float opSubtract(float d1, float d2) { return max(-d1, d2); }

// Smooth union: blended join
float opSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}
```

### Normal Calculation
```glsl
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

### Raymarching Loop
```glsl
float raymarch(vec3 ro, vec3 rd) {
    float d = 0.0;
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * d;
        float ds = sceneSDF(p);
        d += ds;
        if (d > MAX_DIST || ds < SURF_DIST) break;
    }
    return d;
}
```
