# GPU Stamp-Based Brush Rendering Plan

**Date**: 2025-01-05
**Goal**: Implement Procreate-style GPU stamp brush rendering for smooth, high-performance drawing
**Status**: PLANNING

## Executive Summary

Replace current CPU-based tessellation with GPU stamp rendering. This approach:
- Eliminates zigzag artifacts (stamps blend smoothly)
- Dramatically improves performance (GPU parallelism)
- Enables rich brush customization (textures, opacity, blending)
- Matches industry standard (Procreate, Photoshop, etc.)

## Research Sources

- [Brush Rendering Tutorial - Stamp](https://shenciao.github.io/brush-rendering-tutorial/Basics/Stamp/)
- [Efficient Rendering of Linear Brush Strokes (JCGT)](https://apoorvaj.io/efficient-rendering-of-linear-brush-strokes/)
- [Ciallo: GPU-Accelerated Vector Brush Strokes (SIGGRAPH 2024)](https://dl.acm.org/doi/10.1145/3641519.3657418)
- [SDL3 GPU API Documentation](https://wiki.libsdl.org/SDL3/CategoryGPU)
- [MaLiang - Metal Drawing Library](https://github.com/Harley-xk/MaLiang)
- [SDL_shadercross for Metal/Vulkan](https://moonside.games/posts/introducing-sdl-shadercross/)

---

## Part 1: Understanding Stamp Brush Rendering

### How It Works

Traditional drawing apps place "stamps" (circular textures) along the stroke path:

```
Traditional (CPU):          GPU Stamp Approach:
   ○ ○ ○ ○ ○ ○              ████████████████
   (discrete stamps)        (continuous blend)
```

**The Problem with CPU Approach:**
1. Discrete stamps create visible circles at fast movement speeds
2. Overdraw: GPU writes same pixels multiple times (slow)
3. Each stamp is a separate draw call (CPU bottleneck)

**The GPU Stamp Solution:**
1. Render entire stroke as a SINGLE quad
2. Fragment shader computes stamp contributions analytically
3. No overdraw - each pixel computed once
4. Infinitely smooth blending

### The Mathematics

For each pixel at position (X, Y), compute the accumulated alpha:

```
α(X,Y) = ∫[X₁ to X₂] f(x, X, Y) dx

where:
- X₁, X₂ = range of stamp centers affecting this pixel
- f(x, X, Y) = stamp texture intensity at pixel when stamp is at x
```

For discrete stamps (simpler implementation):
```glsl
float alpha = 0.0;
for (each stamp i in range) {
    float dist = distance(pixel, stamp_center[i]);
    float stamp_alpha = texture(brush_texture, dist / radius);
    alpha = alpha * (1.0 - stamp_alpha) + stamp_alpha; // Porter-Duff over
}
```

---

## Part 2: Architecture Design

### Current Architecture (CPU Tessellation)
```
Touch Input → Points → Bezier Tessellation (CPU) → Triangles → SDL_RenderGeometry
```

### New Architecture (GPU Stamps)
```
Touch Input → Points → Polyline → GPU Shader → Stamp Blending → Texture Output
```

### Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Drawing System                            │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────┐  │
│  │ Input Layer │───▶│ Stroke Data │───▶│ GPU Stamp Renderer  │  │
│  │ (touch/pen) │    │ (polyline)  │    │ (Metal shaders)     │  │
│  └─────────────┘    └─────────────┘    └─────────────────────┘  │
│                                                │                 │
│                                                ▼                 │
│                                        ┌─────────────────┐      │
│                                        │ Canvas Texture  │      │
│                                        │ (accumulated)   │      │
│                                        └─────────────────┘      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Part 3: Implementation Options

### Option A: SDL3 GPU API (Recommended)

**Pros:**
- Cross-platform (Metal, Vulkan, D3D12)
- Part of SDL ecosystem we already use
- Future-proof, actively developed
- Shader cross-compilation via SDL_shadercross

**Cons:**
- SDL3 GPU API is new (may have bugs)
- Custom shaders only in SDL 3.4.0+ (for renderer backend)
- Learning curve for new API

**Implementation Path:**
```cpp
// 1. Create GPU device
SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                             SDL_GPU_SHADERFORMAT_MSL,
                                             true, NULL);

// 2. Load shaders (cross-compiled HLSL → MSL/SPIRV)
SDL_GPUShader* vertex_shader = SDL_CreateGPUShaderFromFile(device, "stamp.vert.spv");
SDL_GPUShader* fragment_shader = SDL_CreateGPUShaderFromFile(device, "stamp.frag.spv");

// 3. Create pipeline
SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {
    .vertex_shader = vertex_shader,
    .fragment_shader = fragment_shader,
    // ... blending config for alpha compositing
};
SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);

// 4. Per-stroke rendering
SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
// Upload stroke data, bind textures, draw
```

### Option B: Raw Metal (iOS-specific)

**Pros:**
- Maximum control
- Best iOS performance
- Direct access to Metal features

**Cons:**
- iOS/macOS only
- More complex code
- Need separate Vulkan path for other platforms

**Implementation Path:**
```objc
// 1. Create Metal device and command queue
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
id<MTLCommandQueue> commandQueue = [device newCommandQueue];

// 2. Create render pipeline
MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
desc.vertexFunction = [library newFunctionWithName:@"stamp_vertex"];
desc.fragmentFunction = [library newFunctionWithName:@"stamp_fragment"];
desc.colorAttachments[0].blendingEnabled = YES;
// ... configure alpha blending

// 3. Render stamps
id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoder...];
[encoder setRenderPipelineState:pipelineState];
[encoder setVertexBuffer:strokeBuffer offset:0 atIndex:0];
[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
```

### Option C: Hybrid (SDL_Renderer + Custom Texture)

**Pros:**
- Minimal changes to existing code
- Keep using SDL_Renderer for most things
- Custom GPU work only for strokes

**Cons:**
- Limited by SDL_Renderer capabilities
- May have sync issues between APIs

---

## Part 4: Shader Design

### Vertex Shader (stamp_stroke.vert)

```hlsl
// Input: stroke polyline as line segments
// Output: quad vertices covering each segment

struct VSInput {
    float2 position : POSITION;    // Point position
    float2 prev_pos : TEXCOORD0;   // Previous point (for direction)
    float radius : TEXCOORD1;      // Brush radius at this point
    float distance : TEXCOORD2;    // Cumulative distance from start
};

struct VSOutput {
    float4 position : SV_Position;
    float2 local_pos : TEXCOORD0;  // Position within stroke quad
    float radius : TEXCOORD1;
    float distance : TEXCOORD2;
};

VSOutput main(VSInput input, uint vid : SV_VertexID) {
    // Expand line segment to quad
    float2 dir = normalize(input.position - input.prev_pos);
    float2 perp = float2(-dir.y, dir.x);

    // 4 vertices per segment (triangle strip)
    float2 offsets[4] = {
        -perp * input.radius,
        +perp * input.radius,
        -perp * input.radius,
        +perp * input.radius
    };

    VSOutput output;
    output.position = float4(input.position + offsets[vid], 0, 1);
    output.local_pos = offsets[vid] / input.radius;
    output.radius = input.radius;
    output.distance = input.distance;
    return output;
}
```

### Fragment Shader (stamp_stroke.frag)

```hlsl
// Input: interpolated quad data
// Output: blended stamp color

Texture2D brush_texture : register(t0);
SamplerState brush_sampler : register(s0);

cbuffer StrokeParams : register(b0) {
    float4 brush_color;
    float stamp_spacing;  // Distance between stamps
    float total_length;   // Total stroke length
};

struct PSInput {
    float4 position : SV_Position;
    float2 local_pos : TEXCOORD0;
    float radius : TEXCOORD1;
    float distance : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target {
    float alpha = 0.0;

    // Calculate which stamps can affect this pixel
    float pixel_dist = input.distance;
    int first_stamp = max(0, int((pixel_dist - input.radius) / stamp_spacing));
    int last_stamp = int((pixel_dist + input.radius) / stamp_spacing);

    // Accumulate stamp contributions
    for (int i = first_stamp; i <= last_stamp; i++) {
        float stamp_center = i * stamp_spacing;
        float2 offset = float2(pixel_dist - stamp_center, length(input.local_pos));

        // Sample brush texture
        float2 uv = offset / input.radius * 0.5 + 0.5;
        float stamp_alpha = brush_texture.Sample(brush_sampler, uv).a;

        // Porter-Duff "over" compositing
        alpha = alpha * (1.0 - stamp_alpha) + stamp_alpha;
    }

    return float4(brush_color.rgb, alpha * brush_color.a);
}
```

---

## Part 5: Implementation Steps

### Phase 1: Foundation (Week 1)
1. [ ] Set up SDL3 GPU API in the project
2. [ ] Create basic render pipeline with pass-through shaders
3. [ ] Verify Metal backend works on iOS simulator
4. [ ] Create brush texture loading system

### Phase 2: Basic Stamp Rendering (Week 2)
1. [ ] Implement vertex shader for stroke expansion
2. [ ] Implement fragment shader with single stamp
3. [ ] Test with simple circular brush
4. [ ] Add alpha blending to canvas texture

### Phase 3: Multi-Stamp Accumulation (Week 3)
1. [ ] Implement stamp spacing and accumulation
2. [ ] Add cumulative distance calculation
3. [ ] Optimize stamp range calculation
4. [ ] Test with various brush spacings

### Phase 4: Integration (Week 4)
1. [ ] Replace CPU tessellation with GPU stamps
2. [ ] Update jank interface functions
3. [ ] Handle touch pressure for radius
4. [ ] Performance testing and optimization

### Phase 5: Polish (Week 5)
1. [ ] Add brush texture customization
2. [ ] Implement brush rotation/variation
3. [ ] Add undo/redo support
4. [ ] Document the new system

---

## Part 6: Data Structures

### Stroke Representation
```cpp
struct StrokePoint {
    float x, y;           // Position
    float pressure;       // 0.0 - 1.0
    float distance;       // Cumulative distance from stroke start
};

struct Stroke {
    std::vector<StrokePoint> points;
    uint32_t brush_id;
    float4 color;
    float base_radius;
};

struct BrushParams {
    SDL_GPUTexture* texture;
    float spacing;        // As fraction of radius (0.1 = 10% overlap)
    float opacity;
    float hardness;       // Edge falloff
};
```

### GPU Buffer Layout
```cpp
// Per-vertex data (uploaded to GPU)
struct StrokeVertex {
    float position[2];
    float prev_position[2];
    float radius;
    float distance;
    float pressure;
    float _padding;
};

// Uniform buffer
struct StrokeUniforms {
    float color[4];
    float stamp_spacing;
    float total_length;
    float canvas_size[2];
};
```

---

## Part 7: Integration with jank

### New Native Functions

```cpp
// drawing_canvas.hpp additions

// Initialize GPU stamp renderer
void init_gpu_renderer();

// Load brush texture from file or data
int load_brush_texture(const char* path);
int load_brush_texture_data(const uint8_t* data, int width, int height);

// Set current brush
void set_brush(int brush_id, float spacing, float opacity);

// Begin stroke with GPU rendering
void begin_gpu_stroke(float x, float y, float pressure);
void add_gpu_stroke_point(float x, float y, float pressure);
void end_gpu_stroke();

// Render accumulated strokes to canvas
void render_gpu_strokes();
```

### jank Interface

```clojure
;; vybe.app.drawing.gpu namespace

(defn init-gpu! []
  "Initialize GPU stamp renderer."
  (canvas/init_gpu_renderer))

(defn load-brush! [path]
  "Load brush texture. Returns brush ID."
  (canvas/load_brush_texture path))

(defn set-brush! [brush-id spacing opacity]
  "Set active brush for subsequent strokes."
  (canvas/set_brush brush-id (float spacing) (float opacity)))

(defn begin-stroke! [x y pressure]
  (canvas/begin_gpu_stroke (float x) (float y) (float pressure)))

(defn add-point! [x y pressure]
  (canvas/add_gpu_stroke_point (float x) (float y) (float pressure)))

(defn end-stroke! []
  (canvas/end_gpu_stroke))
```

---

## Part 8: Performance Considerations

### Why GPU Stamps Are Fast

| Aspect | CPU Tessellation | GPU Stamps |
|--------|------------------|------------|
| Draw calls | Many triangles | 1 quad per segment |
| Memory writes | Overdraw (N×) | Each pixel once |
| Computation | CPU-bound | GPU-parallel |
| Scalability | Degrades with stroke length | Constant |

### Optimization Techniques

1. **Instanced rendering**: Draw all stroke segments in one call
2. **Texture atlasing**: Multiple brush textures in one texture
3. **Early-Z culling**: Skip pixels outside stroke bounds
4. **Tile-based rendering**: Good for Metal on iOS (TBR architecture)

---

## Part 9: Fallback Strategy

If SDL3 GPU API proves problematic, fallback options:

1. **MaLiang approach**: Use Metal directly via Objective-C++
2. **Compute shaders**: Use SDL3 compute for stamp blending
3. **Improved CPU**: Keep tessellation but add input smoothing

---

## Part 10: Success Criteria

- [ ] Smooth strokes with NO zigzag artifacts
- [ ] 60 FPS on iPad Pro with complex strokes
- [ ] Brush texture support (custom brushes)
- [ ] Pressure sensitivity working
- [ ] Undo/redo maintained
- [ ] Works on both simulator and device

---

---

## Part 11: Metal Implementation Details (from Bogdan Redkin's Tutorial)

Source: [How to implement Drawing App on iOS using Metal](https://medium.com/@aldammit/how-to-implement-drawing-app-on-ios-using-metal-27f07723bd32)

### Core Metal Components

```
MTLDevice → MTLCommandQueue → MTLCommandBuffer → MTLRenderCommandEncoder → Present
```

1. **MTLDevice**: GPU representation, use `MTLCreateSystemDefaultDevice()`
2. **MTLBuffer**: Send/receive data to/from shaders
3. **MTLCommandQueue**: Stores GPU commands
4. **MTLCommandBuffer**: Temporary container for drawable content
5. **MTLRenderCommandEncoder**: Encodes drawable commands

### Coordinate System Conversion

Metal uses normalized device coordinates (NDC):
- Origin (0,0) at CENTER of viewport
- Range: (-1, -1) bottom-left to (1, 1) top-right

```swift
// UIKit to Metal conversion
func convertToMetal(point: CGPoint, viewSize: CGSize) -> SIMD2<Float> {
    let x = Float(point.x / viewSize.width) * 2.0 - 1.0
    let y = Float(1.0 - point.y / viewSize.height) * 2.0 - 1.0  // Y flipped
    return SIMD2<Float>(x, y)
}
```

### Point Vertex Structure (MSL)

```metal
struct PointVertex {
    float4 position [[position]];
    float pointSize [[point_size]];
    float4 color;
};

vertex PointVertex pointShaderVertex(
    constant Point* points [[ buffer(0) ]],
    uint vid [[ vertex_id ]]
) {
    PointVertex out;
    out.position = float4(points[vid].position, 0.0, 1.0);
    out.pointSize = points[vid].size;
    out.color = points[vid].color;
    return out;
}

fragment half4 pointShaderFragment(
    PointVertex point_data [[ stage_in ]],
    float2 pointCoord [[ point_coord ]]
) {
    // Create smooth circular brush
    float dist = length(pointCoord - float2(0.5));
    float4 out_color = point_data.color;
    out_color.a = 1.0 - smoothstep(0.4, 0.5, dist);  // Soft edge
    return half4(out_color);
}
```

### Smooth Line Interpolation

Key insight: Don't just draw at touch points - interpolate between them!

```swift
func draw(at point: CGPoint) {
    let distance = previousLocation.distance(to: point)
    let pointsPerUnit: CGFloat = 2.0  // Density
    let numberOfPoints = max(1, Int(distance * pointsPerUnit))

    for i in 0..<numberOfPoints {
        let t = CGFloat(i) / CGFloat(numberOfPoints)
        let interpolated = CGPoint(
            x: previousLocation.x + (point.x - previousLocation.x) * t,
            y: previousLocation.y + (point.y - previousLocation.y) * t
        )
        // Add point to buffer
        points.append(Point(position: convertToMetal(interpolated), ...))
    }
    previousLocation = point
}
```

### Render Pipeline State with Blending

```swift
let pipelineDescriptor = MTLRenderPipelineDescriptor()
pipelineDescriptor.vertexFunction = library.makeFunction(name: "pointShaderVertex")
pipelineDescriptor.fragmentFunction = library.makeFunction(name: "pointShaderFragment")
pipelineDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm

// Enable alpha blending
pipelineDescriptor.colorAttachments[0].isBlendingEnabled = true
pipelineDescriptor.colorAttachments[0].rgbBlendOperation = .add
pipelineDescriptor.colorAttachments[0].alphaBlendOperation = .add
pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = .sourceAlpha
pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = .oneMinusSourceAlpha
```

---

## Part 12: Recommended Implementation Approach

Given our constraints (SDL3 + jank + iOS), here's the recommended path:

### Option 1: Metal via Objective-C++ (RECOMMENDED)

Keep SDL3 for window/event handling, but use Metal directly for stroke rendering:

```
SDL3 Window → CAMetalLayer → MTLRenderCommandEncoder → GPU Stamps
     ↓
Touch Events → jank processing → Metal rendering
```

**Why this works:**
1. SDL3's iOS backend already uses CAMetalLayer internally
2. We can extract the Metal layer and use it directly
3. Full control over shaders and blending
4. Matches proven approach (MaLiang, Telegram Drawing)

### Implementation Files Needed

```
src/vybe/app/drawing/native/
├── drawing_canvas.hpp      (existing - keep for SDL events)
├── metal_renderer.h        (NEW - Metal rendering interface)
├── metal_renderer.mm       (NEW - Metal implementation)
└── stamp_shaders.metal     (NEW - MSL shaders)
```

### Integration Strategy

1. **Phase 1**: Create MetalRenderer class that renders to a texture
2. **Phase 2**: Composite Metal texture onto SDL renderer
3. **Phase 3**: Replace CPU tessellation with Metal stamps
4. **Phase 4**: Add brush customization

---

## Next Steps

1. **Immediate**: Verify SDL3 GPU API availability in our build
2. **This session**: Create minimal Metal rendering test in .mm file
3. **Follow-up**: Implement stamp shader prototype

## Commands

```bash
# Check SDL3 version and GPU support
grep -r "SDL_GPU" vendor/SDL/include/

# Check if we have Metal headers
ls /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk/System/Library/Frameworks/Metal.framework/

# Build with GPU API enabled
make drawing-ios-jit-sim-run
```

## References

- [Bogdan Redkin's Metal Drawing Tutorial](https://medium.com/@aldammit/how-to-implement-drawing-app-on-ios-using-metal-27f07723bd32)
- [Part 2: Dynamic Line Size & Eraser](https://medium.com/@aldammit/part-2-enhancing-your-draw-app-with-dynamic-line-size-eraser-and-color-size-customization-4dd0e6e8e7e4)
- [GitHub: iOS-Metal-Drawing-tutorial](https://github.com/aldammit/iOS-Metal-Drawing-tutorial)
