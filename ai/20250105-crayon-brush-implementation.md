# Crayon Brush Implementation - Metal Shader Pipelines

**Date**: 2025-01-05

## What I Learned

### 1. Multiple Metal Render Pipelines for Brush Types

Each brush type (Round, Crayon, Watercolor, Marker) requires its own Metal render pipeline with a different fragment shader:

```objc
// In MetalStampRendererImpl interface
@property (nonatomic, strong) id<MTLRenderPipelineState> stampPipeline;        // Round brush
@property (nonatomic, strong) id<MTLRenderPipelineState> crayonPipeline;       // Crayon brush
@property (nonatomic, strong) id<MTLRenderPipelineState> watercolorPipeline;   // Watercolor brush
@property (nonatomic, strong) id<MTLRenderPipelineState> markerPipeline;       // Marker brush
```

Pipelines are created in `createPipelines` by getting different fragment functions from the same library:

```objc
id<MTLFunction> crayonFragmentFunc = [library newFunctionWithName:@"stamp_fragment_crayon"];
pipelineDesc.fragmentFunction = crayonFragmentFunc;
self.crayonPipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
```

### 2. Pipeline Selection at Render Time

In `renderPointsWithHardness`, select pipeline based on `currentBrushType`:

```objc
id<MTLRenderPipelineState> pipeline = self.stampPipeline;
switch (self.currentBrushType) {
    case 0: pipeline = self.stampPipeline; break;
    case 1: pipeline = self.crayonPipeline ? self.crayonPipeline : self.stampPipeline; break;
    case 2: pipeline = self.watercolorPipeline ? self.watercolorPipeline : self.stampPipeline; break;
    case 3: pipeline = self.markerPipeline ? self.markerPipeline : self.stampPipeline; break;
}
[encoder setRenderPipelineState:pipeline];
```

### 3. Procedural Crayon Texture in Metal Shader

The crayon shader uses procedural noise (sin waves) to simulate paper grain:

```metal
fragment half4 stamp_fragment_crayon(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    // Circle mask
    float2 centered = pointCoord - float2(0.5);
    float dist = length(centered) * 2.0;
    float circleMask = 1.0 - smoothstep(inner_radius, 1.0, dist);

    // Procedural grain using sin waves (simulates paper texture)
    float2 noiseCoord = pointCoord * 20.0 + uniforms.grainOffset;
    float grain1 = sin(noiseCoord.x * 17.3 + noiseCoord.y * 23.7) * 0.5 + 0.5;
    float grain2 = sin(noiseCoord.x * 31.1 + noiseCoord.y * 11.3) * 0.5 + 0.5;
    float grainValue = (grain1 * 0.4 + grain2 * 0.35 + grain3 * 0.25);

    // Apply grain scale
    float textureStrength = uniforms.grainScale * 0.5;
    float finalGrain = mix(1.0, grainValue, textureStrength);

    out_color.a *= circleMask * finalGrain * uniforms.opacity * uniforms.flow;
}
```

### 4. BrushType Enum Pattern

Using an enum class for type-safe brush selection:

```cpp
enum class BrushType : int32_t {
    Round = 0,
    Crayon = 1,
    Watercolor = 2,
    Marker = 3
};
```

Default brush type set in `BrushSettings`:
```cpp
struct BrushSettings {
    BrushType type = BrushType::Crayon;  // Default to crayon!
    // ...
};
```

### 5. iOS Simulator Install Issues

The `make drawing-ios-jit-sim-run` makefile target uses `find` to locate the .app bundle, which can fail if the path has issues. Direct install works better:

```bash
xcrun simctl install 'iPad Pro 13-inch (M4)' "/path/to/DrawingMobile-JIT-Sim.app"
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

### 6. Verifying Metal Shader Compilation

Check what functions are in a compiled metallib:

```bash
xcrun metal-nm /path/to/default.metallib
# Output shows function symbols:
# 000002dc T stamp_fragment_crayon
# 00000383 T stamp_fragment_watercolor
```

## Files Modified

| File | Changes |
|------|---------|
| `metal_renderer.h` | Added `BrushType` enum, added `type` field to `BrushSettings`, added `set_brush_type()` method and extern "C" wrapper |
| `metal_renderer.mm` | Added crayon/watercolor/marker pipelines, implemented pipeline selection in render, added `set_brush_type` implementation |
| `stamp_shaders.metal` | Already had `stamp_fragment_crayon`, `stamp_fragment_watercolor`, `stamp_fragment_marker` shaders |
| `drawing_mobile_ios.mm` | Added `metal_stamp_set_brush_type(1)` and `metal_stamp_set_brush_grain_scale(2.0f)` for crayon brush |

## Commands Run

```bash
# Build iOS simulator app
make drawing-ios-jit-sim-build

# Direct install and launch (more reliable than makefile target)
xcrun simctl install 'iPad Pro 13-inch (M4)' "/path/to/DrawingMobile-JIT-Sim.app"
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim

# Check metallib contents
xcrun metal-nm /path/to/default.metallib
```

## What's Next

- Phase 2: Implement grain texture loading (image-based textures vs procedural)
- Phase 3: Apple Pencil pressure dynamics
- Phase 4: Scatter, jitter, tilt support
- Future: Undo-tree architecture for non-destructive editing
