# Brush System Research: Procreate-Style Custom Brushes

**Date**: 2025-01-05
**Goal**: Understand how Procreate and other digital painting apps implement custom brushes, then design a similar system for our Metal-based drawing app.

---

## Executive Summary

Procreate's brush system is built on a **dual-texture stamp architecture**:
1. **Shape** (stamp silhouette) - defines the brush tip outline
2. **Grain** (interior texture) - fills the shape with texture

Each brush stroke is rendered by **stamping** these combined textures along the stroke path at configurable intervals. This is augmented by **dynamics** that respond to Apple Pencil pressure, tilt, azimuth, and speed.

---

## 1. Core Brush Architecture

### The Stamp Paradigm

> "Procreate makes brushes with a shape (container) that holds a grain (texture). You can see the shape of any brush when you tap your finger or Apple Pencil on the canvas. By doing this, you create a stamp instead of making a stroke. Dragging this shape creates a stroke."

**Key Insight**: Over 90% of digital brushes are stamp-based. The brush stroke is simply many stamps placed along a path.

### Two-Texture System

```
┌─────────────────────────────────────────┐
│              BRUSH STAMP                │
│  ┌─────────────┐   ┌─────────────┐     │
│  │   SHAPE     │ × │   GRAIN     │     │
│  │ (silhouette)│   │  (texture)  │     │
│  │             │   │             │     │
│  │  grayscale  │   │  grayscale  │     │
│  │   PNG       │   │  tiling PNG │     │
│  └─────────────┘   └─────────────┘     │
│         ↓                 ↓            │
│    opacity mask    texture fill        │
└─────────────────────────────────────────┘
```

### Shape Source
- **Grayscale PNG** (typically 512x512 or 1024x1024)
- **White** = fully opaque areas
- **Black** = transparent areas
- **Gray values** = partial opacity
- Controls the **silhouette** of each stamp
- A smooth circular shape → smooth stroke
- An irregular patchy shape → rough, textured stroke

### Grain Source
- **Grayscale tiling PNG** that fills inside the shape
- Acts like a "paint roller" texture
- Two behavior modes:
  - **Moving**: Texture drags with the stroke (paint-like)
  - **Texturized**: Static texture behind stroke (stencil-like)

---

## 2. Stamp Spacing & Interpolation

### Spacing Algorithm

```
stamp_interval = brush_diameter × (spacing_percentage / 100)
```

**Common spacing values**:
| Spacing | Effect |
|---------|--------|
| 1-10%   | Very dense, smooth strokes (expensive) |
| 25%     | Default for smooth appearance |
| 100%    | Stamps just touch |
| >100%   | Visible gaps (dotted effect) |

### Distance-Based Stamping

```cpp
// Pseudocode for stamp placement
accumulated_distance = 0;
for each new_point from input:
    distance = length(new_point - last_point);
    accumulated_distance += distance;

    while (accumulated_distance >= stamp_interval):
        // Calculate interpolated position
        t = stamp_interval / accumulated_distance;
        stamp_pos = lerp(last_point, new_point, t);

        // Interpolate other properties
        stamp_pressure = lerp(last_pressure, new_pressure, t);
        stamp_size = base_size * pressure_curve(stamp_pressure);

        place_stamp(stamp_pos, stamp_size, stamp_pressure);

        accumulated_distance -= stamp_interval;

    last_point = new_point;
```

---

## 3. Apple Pencil Dynamics

### Pressure
- Range: 0.0 to 1.0 (or 0-100%)
- Controls: Size, Opacity, Flow, Grain intensity
- Customizable via **pressure curve graph** (up to 4 control nodes)
- Linear vs exponential response

### Tilt
- Range: 0° (flat) to 90° (upright)
- Note: 0-15° is physically impossible (tip doesn't touch)
- 16-30° may be inaccurate
- Controls: Size, Opacity, Shape rotation, Grain blending

### Azimuth
- Detects the **rotation angle** of the pencil around vertical axis
- Creates calligraphy-like effects
- Overrides manual rotation settings

### Speed (Velocity)
- Faster strokes → different size/opacity
- Slider from -100% to +100%:
  - Negative: Slow = thin, Fast = thick
  - Positive: Slow = thick, Fast = thin

### Jitter (Randomness)
- Adds unpredictability without input devices
- Random size, opacity, rotation per stamp
- Good for finger painting without pressure

---

## 4. GPU Rendering Approach

### From SIGGRAPH 2024: Ciallo Paper

The state-of-the-art approach from [Ciallo: GPU-Accelerated Rendering of Vector Brush Strokes](https://dl.acm.org/doi/10.1145/3641519.3657418):

#### Vertex Shader
- Takes polyline edges as input
- Expands each edge into a quad (4 vertices)
- Passes edge parameters to fragment shader

#### Fragment Shader
- **Calculates stamp positions per-pixel** (not per-vertex!)
- Uses prefix sum for cumulative edge lengths
- For each pixel, determines which stamps can affect it
- Loops through relevant stamps, samples textures, composites

```glsl
// Simplified fragment shader concept
fragment half4 stamp_fragment(
    float2 pixel_pos,
    float2 edge_start,
    float2 edge_end,
    float stamp_interval,
    texture2d<float> shape_tex,
    texture2d<float> grain_tex
) {
    // Calculate how many stamps on this edge
    float edge_length = distance(edge_start, edge_end);
    int num_stamps = int(edge_length / stamp_interval);

    half4 result = half4(0);

    // Loop through stamps that could affect this pixel
    for (int i = 0; i < num_stamps; i++) {
        float t = float(i) / float(num_stamps);
        float2 stamp_center = mix(edge_start, edge_end, t);

        float dist = distance(pixel_pos, stamp_center);
        if (dist < stamp_radius) {
            // Sample shape and grain
            float2 local_uv = (pixel_pos - stamp_center) / stamp_radius * 0.5 + 0.5;
            float shape_alpha = shape_tex.sample(sampler, local_uv).a;
            float grain_value = grain_tex.sample(sampler, local_uv).r;

            // Combine
            half4 stamp_color = brush_color;
            stamp_color.a *= shape_alpha * grain_value;

            // Alpha composite
            result = result + stamp_color * (1 - result.a);
        }
    }

    return result;
}
```

### Optimization: Range Calculation

To avoid iterating ALL stamps, solve a quadratic equation to find which stamps can possibly affect each pixel:

```
ax² + bx + c = 0
where:
  a = 1 - cos²θ
  b = 2(r₀cosθ - xₚ)
  c = xₚ² + yₚ² - r₀²
```

The two roots define the range of stamp indices to check.

---

## 5. Brush Parameters to Implement

### Essential (Phase 1)
| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `size` | float | 1-500px | Base brush diameter |
| `opacity` | float | 0-1 | Overall stroke opacity |
| `hardness` | float | 0-1 | Edge softness (0=soft, 1=hard) |
| `spacing` | float | 0.01-2.0 | Stamp interval as % of diameter |
| `color` | RGBA | - | Brush color |

### Pressure Dynamics (Phase 2)
| Parameter | Type | Description |
|-----------|------|-------------|
| `size_pressure` | float | How much pressure affects size |
| `opacity_pressure` | float | How much pressure affects opacity |
| `pressure_curve` | [4 points] | Custom pressure response |

### Shape/Grain (Phase 3)
| Parameter | Type | Description |
|-----------|------|-------------|
| `shape_texture` | texture | Brush tip shape (grayscale) |
| `grain_texture` | texture | Fill texture (grayscale, tiling) |
| `grain_scale` | float | Texture scale within shape |
| `grain_mode` | enum | Moving vs Texturized |
| `shape_rotation` | float | Base rotation |
| `shape_randomize` | float | Random rotation per stamp |

### Advanced (Phase 4)
| Parameter | Type | Description |
|-----------|------|-------------|
| `flow` | float | Paint flow rate (vs opacity) |
| `wet_edges` | bool | Darker edges effect |
| `scatter` | float | Random offset from path |
| `count` | int | Multiple stamps per position |
| `tilt_size` | float | Tilt affects size |
| `tilt_opacity` | float | Tilt affects opacity |
| `velocity_size` | float | Speed affects size |
| `jitter_size` | float | Random size variation |
| `jitter_opacity` | float | Random opacity variation |

---

## 6. Texture Requirements

### Shape Texture
- **Format**: Grayscale PNG, single channel or RGBA with alpha
- **Size**: 512x512 recommended (power of 2)
- **Content**: White = visible, Black = transparent
- **Edge**: Can have soft antialiased edges

### Grain Texture
- **Format**: Grayscale PNG, **must tile seamlessly**
- **Size**: 256x256 to 1024x1024
- **Content**: Texture pattern
- **Tiling**: Edges must match perfectly

### Creating Seamless Textures
1. Start with texture image
2. Apply **Offset filter** (half width, half height, wrap around)
3. Fix seams in center using clone/heal tools
4. Repeat offset to verify seamlessness

---

## 7. Procreate .brush File Format

Procreate brushes are ZIP archives containing:

```
mybush.brush (renamed .zip)
├── Brush.archive      # Binary plist with all settings
├── Shape.png          # Shape source (grayscale)
├── Grain.png          # Grain source (grayscale, tiling)
└── QuickLook/
    └── Thumbnail.png  # Preview image
```

### Brush.archive Structure
Contains serialized Objective-C objects with properties like:
- `brushSize`, `brushOpacity`, `brushSpacing`
- `pressureSizeResponse`, `pressureOpacityResponse`
- `shapeRotation`, `shapeScatter`
- `grainScale`, `grainMovement`
- And 100+ other parameters

---

## 8. Implementation Plan

### Phase 1: Enhanced Current System
**Goal**: Add texture support to existing stamp renderer

1. Load shape texture (grayscale PNG)
2. Sample shape in fragment shader for alpha
3. Add spacing control
4. Add rotation parameter

```metal
// Phase 1 fragment shader
fragment half4 textured_stamp(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    texture2d<float> shapeTexture [[texture(0)]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    // Sample shape texture for alpha mask
    constexpr sampler s(filter::linear);
    float shape_alpha = shapeTexture.sample(s, pointCoord).r;

    // Apply hardness falloff on top
    float2 centered = pointCoord - 0.5;
    float dist = length(centered) * 2.0;
    float falloff = 1.0 - smoothstep(uniforms.hardness, 1.0, dist);

    float final_alpha = shape_alpha * falloff * uniforms.opacity;

    if (final_alpha <= 0.001) discard_fragment();

    return half4(in.color.rgb, in.color.a * final_alpha);
}
```

### Phase 2: Dual Texture System
**Goal**: Add grain texture support

1. Load grain texture (tiling grayscale PNG)
2. Calculate grain UV based on world position (for Moving mode)
3. Multiply shape × grain for final alpha

### Phase 3: Pressure Dynamics
**Goal**: Respond to Apple Pencil pressure

1. Pass pressure from touch events
2. Apply pressure curves to size, opacity
3. Interpolate pressure between points

### Phase 4: Advanced Features
- Scatter/jitter
- Multiple stamp count
- Tilt response
- Wet edges
- Dual brush combining

---

## 9. Data Structures

### Brush Preset (Rust/C++ struct)
```cpp
struct BrushPreset {
    // Identity
    char name[64];
    uint32_t id;

    // Core
    float size;           // 1-500
    float opacity;        // 0-1
    float hardness;       // 0-1
    float spacing;        // 0.01-2.0
    float4 color;         // RGBA

    // Textures
    uint32_t shape_texture_id;   // 0 = default circle
    uint32_t grain_texture_id;   // 0 = none
    float grain_scale;
    bool grain_moving;           // true = Moving, false = Texturized

    // Shape dynamics
    float shape_rotation;        // degrees
    float shape_rotation_jitter; // random range
    float scatter;               // offset from path

    // Pressure response
    float size_pressure;         // -1 to 1
    float opacity_pressure;      // -1 to 1
    float4 pressure_curve;       // 4 control points

    // Velocity
    float size_velocity;         // -1 to 1
    float opacity_velocity;

    // Jitter
    float size_jitter;
    float opacity_jitter;
};
```

### Stamp Point (GPU buffer)
```cpp
struct StampPoint {
    float2 position;     // NDC coordinates
    float size;          // Diameter in pixels
    float rotation;      // Radians
    float4 color;        // RGBA with computed opacity
    float2 grain_offset; // For moving grain mode
};
```

---

## 10. References & Sources

### Official Documentation
- [Procreate Brush Studio Settings](https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings)
- [Procreate Apple Pencil](https://help.procreate.com/procreate/handbook/interface-gestures/pencil)
- [Procreate Dual Brush](https://help.procreate.com/procreate/handbook/brushes/dual-brush)

### Research Papers
- [Ciallo: GPU-Accelerated Rendering of Vector Brush Strokes (SIGGRAPH 2024)](https://dl.acm.org/doi/10.1145/3641519.3657418)
- [Wetbrush: GPU-based 3D Painting Simulation](https://dl.acm.org/doi/10.1145/2816795.2818066)

### Tutorials & Guides
- [Brush Rendering Tutorial - Stamp](https://shenciao.github.io/brush-rendering-tutorial/Basics/Stamp/)
- [Krita Brush Settings Documentation](https://docs.krita.org/en/reference_manual/brushes/brush_settings.html)
- [RetroSupply Ultimate Procreate Brush Guide](https://www.retrosupply.co/blogs/tutorials/ultimate-procreate-brush-guide)
- [Liz Kohler Brown - How To Create Any Brush in Procreate](https://www.lizkohlerbrown.com/how-to-create-any-brush-in-procreate/)

---

## 11. Undo-Tree Architecture (Emacs-style)

### Why Undo-Tree vs Linear Undo

Traditional undo is **linear**: Undo removes the last action, redo restores it. If you undo and then make a new action, the redo history is lost.

**Undo-tree** (like Emacs) preserves ALL history as a tree:
- Every state is a node
- Undo moves to parent node
- New actions create new branches
- You can navigate to ANY previous state

```
              [Initial]
                  │
              [Stroke 1]
                  │
              [Stroke 2]
                  │
         ┌───────┴───────┐
     [Stroke 3]      [Stroke 3b]  ← branched after undo
         │               │
     [Stroke 4]      [Stroke 4b]
                         │
                   [Current] ← we're here
```

### Data Model

```cpp
struct CanvasState {
    uint32_t id;
    uint32_t parent_id;           // 0 for root
    std::vector<uint32_t> children;
    uint64_t timestamp;

    // Delta storage (not full canvas)
    std::vector<StrokeData> added_strokes;
    std::vector<uint32_t> removed_stroke_ids;

    // Or for full snapshots (less frequent)
    MTLTexture* snapshot;         // Compressed canvas state
};

struct UndoTree {
    std::unordered_map<uint32_t, CanvasState> nodes;
    uint32_t current_node_id;
    uint32_t root_id;
    uint32_t next_id;

    // Navigation
    void undo();                  // Go to parent
    void redo(int branch = 0);    // Go to child (default = last used)
    void goto_node(uint32_t id);  // Jump to any state

    // Modification
    uint32_t commit(StrokeData stroke);  // Create new node

    // Visualization
    std::vector<UndoTreeNode> get_tree_structure();
};
```

### Storage Strategy

**Hybrid approach**:
1. **Stroke-based deltas** (small): Store added/removed strokes per node
2. **Periodic snapshots** (larger): Every N nodes, store compressed canvas texture
3. **Lazy reconstruction**: To reach a state, find nearest snapshot, apply deltas

```cpp
MTLTexture* reconstruct_canvas(uint32_t target_node_id) {
    // Find path from nearest snapshot to target
    auto path = find_path_from_snapshot(target_node_id);

    // Start from snapshot
    MTLTexture* canvas = decompress_snapshot(path.snapshot_id);

    // Apply deltas
    for (auto& delta : path.deltas) {
        apply_delta(canvas, delta);
    }

    return canvas;
}
```

### Integration with Brush System

Every stroke completion triggers:
1. `undo_tree.commit(current_stroke_data)`
2. New node created as child of current
3. Current pointer moves to new node

Undo/Redo operations:
1. Navigate tree (change current pointer)
2. Reconstruct canvas state
3. Re-render

### Memory Management

- **Max nodes**: Configurable limit (e.g., 1000)
- **Pruning**: Remove oldest branches when limit reached (keep main line)
- **Compression**: LZ4 for snapshots, store stroke deltas efficiently
- **Disk persistence**: Save tree to file for session recovery

---

## 12. Next Steps

1. **Implement Phase 1**: Add shape texture sampling to current shader
2. **Create test brushes**: Make a few shape PNGs (circle, square, splatter)
3. **Add spacing control**: Make stamp interval configurable from jank
4. **Test on iOS**: Verify texture loading works on device
5. **Design brush preset format**: JSON or binary for storing brush configs
6. **Build brush picker UI**: Simple list to switch between presets
7. **Implement undo-tree**: Start with basic linear undo, evolve to tree
