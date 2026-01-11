# Onion Skin Implementation Plan

## Overview

Onion skinning is an animation technique that shows semi-transparent overlays of previous and/or next frames to help animators maintain consistency in motion. This document details how to implement onion skin functionality for the iOS drawing app.

## Current Architecture

### Rendering Pipeline

```
┌──────────────┐    ┌─────────────────┐    ┌──────────────────┐    ┌──────────────┐
│ Stroke Input │ -> │ Canvas Texture  │ -> │ Canvas Blit      │ -> │ Screen       │
│ (Points)     │    │ (3840x2160)     │    │ (with transform) │    │ (Drawable)   │
└──────────────┘    └─────────────────┘    └──────────────────┘    └──────────────┘
                           ↑                       │
                    ┌──────┴───────┐               │
                    │ Frame Cache  │               │
                    │ (12 textures)│               │
                    │ GPU-to-GPU   │               │
                    └──────────────┘               │
                                                   ↓
                                          ┌──────────────────┐
                                          │ UI Overlays      │
                                          │ (Sliders, etc)   │
                                          └──────────────────┘
```

### Key Components

1. **Canvas Texture** (`canvasTexture`): Current frame being drawn (3840x2160 BGRA)
2. **Frame Texture Cache** (`frameTextureCache`): 12 pre-allocated GPU textures for animation frames
3. **Canvas Blit Shader**: Renders canvas to screen with pan/zoom/rotate transform
4. **Present Function**: Composites everything to the screen drawable

### Relevant Code Locations

| Component | File | Line |
|-----------|------|------|
| Frame cache init | `metal_renderer.mm` | 1382-1412 |
| Frame cache access | `metal_renderer.mm` | 1444-1476 |
| Canvas blit shader | `stamp_shaders.metal` | 398-478 |
| Present function | `metal_renderer.mm` | 1838-1883 |
| Transform uniforms | `stamp_shaders.metal` | 401-408 |

---

## Implementation Strategy

### Approach: GPU Frame Texture Compositing

The most efficient approach is to composite cached frame textures directly in the GPU, with color tinting and opacity control. This leverages the existing frame cache (already allocated) and requires minimal new code.

### New Components Needed

1. **Onion Skin Shader** - New fragment shader for tinted frame overlay
2. **Onion Skin State** - Settings (enabled, count, opacity, colors)
3. **Composite Function** - Draws onion frames before current canvas
4. **C API** - Expose settings to the app layer

---

## Detailed Implementation

### Step 1: Add Onion Skin Shader to `stamp_shaders.metal`

```metal
// =============================================================================
// Onion Skin Overlay Shader
// =============================================================================

struct OnionSkinUniforms {
    float2 pan;
    float scale;
    float rotation;
    float2 pivot;
    float2 viewportSize;
    float2 canvasSize;
    float opacity;        // 0.0 - 1.0
    float4 tintColor;     // RGBA tint (e.g., red for past, blue for future)
};

vertex CanvasBlitVertexOut onion_skin_vertex(
    uint vid [[vertex_id]],
    constant OnionSkinUniforms& transform [[buffer(0)]]
) {
    // Same as canvas_blit_vertex but uses OnionSkinUniforms
    float2 corners[4] = {
        float2(-1, -1),
        float2( 1, -1),
        float2(-1,  1),
        float2( 1,  1)
    };

    float2 pos = corners[vid];
    float2 screenPos;
    screenPos.x = (pos.x + 1.0) * 0.5 * transform.viewportSize.x;
    screenPos.y = (1.0 - pos.y) * 0.5 * transform.viewportSize.y;

    float2 p = screenPos - transform.pivot;
    p = p - transform.pan;
    p = p / transform.scale;
    float c = cos(-transform.rotation);
    float s = sin(-transform.rotation);
    p = float2(p.x * c - p.y * s, p.x * s + p.y * c);
    p = p + transform.pivot;

    float2 uv = p / transform.canvasSize;

    CanvasBlitVertexOut out;
    out.position = float4(corners[vid], 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment half4 onion_skin_fragment(
    CanvasBlitVertexOut in [[stage_in]],
    texture2d<float> frameTexture [[texture(0)]],
    sampler frameSampler [[sampler(0)]],
    constant OnionSkinUniforms& uniforms [[buffer(0)]]
) {
    // Out of bounds = transparent
    if (in.uv.x < 0.0 || in.uv.x > 1.0 || in.uv.y < 0.0 || in.uv.y > 1.0) {
        return half4(0, 0, 0, 0);
    }

    float4 color = frameTexture.sample(frameSampler, in.uv);

    // Skip near-white pixels (paper background) - don't show them in onion skin
    float brightness = (color.r + color.g + color.b) / 3.0;
    if (brightness > 0.95) {
        return half4(0, 0, 0, 0);
    }

    // Apply tint color (multiply RGB, preserve structure)
    float3 tinted = color.rgb * uniforms.tintColor.rgb;

    // Alternative: Grayscale + tint overlay
    // float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    // float3 tinted = mix(float3(gray), uniforms.tintColor.rgb, 0.7);

    // Apply opacity
    float alpha = color.a * uniforms.opacity * uniforms.tintColor.a;

    return half4(half3(tinted), half(alpha));
}
```

### Step 2: Add Onion Skin State to `metal_renderer.mm`

```objc
// In @interface MetalStampRendererImpl

// Onion skin properties
@property (nonatomic, assign) BOOL onionSkinEnabled;
@property (nonatomic, assign) int onionSkinPrevCount;  // Show N previous frames
@property (nonatomic, assign) int onionSkinNextCount;  // Show N next frames
@property (nonatomic, assign) float onionSkinOpacity;  // Base opacity (0.0-1.0)
@property (nonatomic, assign) simd_float4 onionSkinPrevColor;  // Tint for past frames
@property (nonatomic, assign) simd_float4 onionSkinNextColor;  // Tint for future frames
@property (nonatomic, strong) id<MTLRenderPipelineState> onionSkinPipeline;

// Onion skin uniforms (matches shader struct)
struct OnionSkinUniforms {
    simd_float2 pan;
    float scale;
    float rotation;
    simd_float2 pivot;
    simd_float2 viewportSize;
    simd_float2 canvasSize;
    float opacity;
    simd_float4 tintColor;
};
```

### Step 3: Initialize Onion Skin Pipeline

Add to `initialize` method after other pipeline creation:

```objc
// Create onion skin pipeline
{
    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = [library newFunctionWithName:@"onion_skin_vertex"];
    desc.fragmentFunction = [library newFunctionWithName:@"onion_skin_fragment"];
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Alpha blending for overlay
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    NSError* error = nil;
    self.onionSkinPipeline = [self.device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!self.onionSkinPipeline) {
        NSLog(@"Failed to create onion skin pipeline: %@", error);
    }
}

// Default onion skin settings
self.onionSkinEnabled = NO;
self.onionSkinPrevCount = 2;
self.onionSkinNextCount = 1;
self.onionSkinOpacity = 0.3;
self.onionSkinPrevColor = simd_make_float4(1.0, 0.3, 0.3, 1.0);  // Reddish
self.onionSkinNextColor = simd_make_float4(0.3, 0.5, 1.0, 1.0);  // Bluish
```

### Step 4: Add Onion Skin Composite Method

```objc
- (void)drawOnionSkinFramesToTexture:(id<MTLTexture>)targetTexture {
    if (!self.onionSkinEnabled || !self.onionSkinPipeline) return;
    if (!self.frameTextureCache || self.frameTextureCache.count == 0) return;

    int currentFrame = self.activeFrameIndex;
    int totalFrames = (int)self.frameTextureCache.count;

    // Build list of frames to draw (back to front for proper compositing)
    NSMutableArray<NSNumber*>* framesToDraw = [NSMutableArray array];
    NSMutableArray<NSNumber*>* frameOpacities = [NSMutableArray array];
    NSMutableArray<NSValue*>* frameColors = [NSMutableArray array];

    // Previous frames (farthest first)
    for (int i = self.onionSkinPrevCount; i >= 1; i--) {
        int frameIdx = (currentFrame - i + totalFrames) % totalFrames;
        if (frameIdx != currentFrame && [self isFrameCached:frameIdx]) {
            [framesToDraw addObject:@(frameIdx)];
            // Opacity falls off with distance
            float opacityFalloff = 1.0 - ((float)(i - 1) / (float)self.onionSkinPrevCount);
            [frameOpacities addObject:@(self.onionSkinOpacity * opacityFalloff)];
            [frameColors addObject:[NSValue valueWithBytes:&_onionSkinPrevColor objCType:@encode(simd_float4)]];
        }
    }

    // Next frames (closest first)
    for (int i = 1; i <= self.onionSkinNextCount; i++) {
        int frameIdx = (currentFrame + i) % totalFrames;
        if (frameIdx != currentFrame && [self isFrameCached:frameIdx]) {
            [framesToDraw addObject:@(frameIdx)];
            float opacityFalloff = 1.0 - ((float)(i - 1) / (float)self.onionSkinNextCount);
            [frameOpacities addObject:@(self.onionSkinOpacity * opacityFalloff)];
            [frameColors addObject:[NSValue valueWithBytes:&_onionSkinNextColor objCType:@encode(simd_float4)]];
        }
    }

    if (framesToDraw.count == 0) return;

    // Draw each onion skin frame
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    for (NSUInteger i = 0; i < framesToDraw.count; i++) {
        int frameIdx = [framesToDraw[i] intValue];
        float opacity = [frameOpacities[i] floatValue];
        simd_float4 tintColor;
        [frameColors[i] getValue:&tintColor];

        id<MTLTexture> frameTexture = self.frameTextureCache[frameIdx];
        if (!frameTexture) continue;

        // Setup render pass (load existing content, don't clear)
        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = targetTexture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
        [encoder setRenderPipelineState:self.onionSkinPipeline];

        // Set uniforms (same transform as current canvas)
        OnionSkinUniforms uniforms = {
            .pan = _canvasTransform.pan,
            .scale = _canvasTransform.scale,
            .rotation = _canvasTransform.rotation,
            .pivot = _canvasTransform.pivot,
            .viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight),
            .canvasSize = simd_make_float2(self.canvasWidth, self.canvasHeight),
            .opacity = opacity,
            .tintColor = tintColor
        };

        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
        [encoder setFragmentTexture:frameTexture atIndex:0];
        [encoder setFragmentSamplerState:self.textureSampler atIndex:0];

        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [encoder endEncoding];
    }

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}
```

### Step 5: Integrate into Present Function

Modify `present()` in `MetalStampRenderer`:

```cpp
void MetalStampRenderer::present() {
    if (!is_ready()) return;

    @autoreleasepool {
        id<CAMetalDrawable> drawable = [impl_.metalLayer nextDrawable];
        if (!drawable) {
            return;
        }

        // 1. Clear to background color first
        // (Need to add a clear pass before onion skin)
        [impl_ clearDrawable:drawable.texture];

        // 2. Draw onion skin frames (semi-transparent overlays)
        [impl_ drawOnionSkinFramesToTexture:drawable.texture];

        // 3. Draw current canvas (with transform)
        if ([impl_ hasCanvasTransform] && impl_.canvasBlitPipeline) {
            [impl_ drawCanvasToTexture:drawable.texture withLoadAction:MTLLoadActionLoad];
        } else {
            // Direct blit...
        }

        // 4. Draw UI overlays
        [impl_ drawQueuedUIRectsToTexture:drawable.texture];
        [impl_ clearUIQueue];

        // 5. Present
        id<MTLCommandBuffer> presentBuffer = [impl_.commandQueue commandBuffer];
        [presentBuffer presentDrawable:drawable];
        [presentBuffer commit];
    }
}
```

### Step 6: Add C API for Settings

```cpp
// In metal_renderer.h
METAL_EXPORT void metal_stamp_set_onion_skin_enabled(bool enabled);
METAL_EXPORT void metal_stamp_set_onion_skin_prev_count(int count);
METAL_EXPORT void metal_stamp_set_onion_skin_next_count(int count);
METAL_EXPORT void metal_stamp_set_onion_skin_opacity(float opacity);
METAL_EXPORT void metal_stamp_set_onion_skin_prev_color(float r, float g, float b, float a);
METAL_EXPORT void metal_stamp_set_onion_skin_next_color(float r, float g, float b, float a);
METAL_EXPORT bool metal_stamp_get_onion_skin_enabled();

// In metal_renderer.mm
METAL_EXPORT void metal_stamp_set_onion_skin_enabled(bool enabled) {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        metal_stamp::g_metal_renderer->impl_.onionSkinEnabled = enabled;
    }
}

METAL_EXPORT void metal_stamp_set_onion_skin_prev_count(int count) {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        metal_stamp::g_metal_renderer->impl_.onionSkinPrevCount = MAX(0, MIN(count, 5));
    }
}

METAL_EXPORT void metal_stamp_set_onion_skin_next_count(int count) {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        metal_stamp::g_metal_renderer->impl_.onionSkinNextCount = MAX(0, MIN(count, 5));
    }
}

METAL_EXPORT void metal_stamp_set_onion_skin_opacity(float opacity) {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        metal_stamp::g_metal_renderer->impl_.onionSkinOpacity = MAX(0.0f, MIN(opacity, 1.0f));
    }
}

METAL_EXPORT void metal_stamp_set_onion_skin_prev_color(float r, float g, float b, float a) {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        metal_stamp::g_metal_renderer->impl_.onionSkinPrevColor = simd_make_float4(r, g, b, a);
    }
}

METAL_EXPORT void metal_stamp_set_onion_skin_next_color(float r, float g, float b, float a) {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        metal_stamp::g_metal_renderer->impl_.onionSkinNextColor = simd_make_float4(r, g, b, a);
    }
}

METAL_EXPORT bool metal_stamp_get_onion_skin_enabled() {
    if (metal_stamp::g_metal_renderer && metal_stamp::g_metal_renderer->impl_) {
        return metal_stamp::g_metal_renderer->impl_.onionSkinEnabled;
    }
    return false;
}
```

---

## UI Integration Options

### Option A: Gesture Toggle

Add to `drawing_mobile_ios.mm`:

```cpp
// Four-finger tap to toggle onion skin
if (fingerCount == 4 && touchDuration < 300) {
    bool current = metal_stamp_get_onion_skin_enabled();
    metal_stamp_set_onion_skin_enabled(!current);
    std::cout << "Onion skin: " << (current ? "OFF" : "ON") << std::endl;
}
```

### Option B: UI Button

Add an onion skin toggle button in the UI overlay area.

### Option C: REPL Control (For Testing)

Expose via jank:
```clojure
(metal/set-onion-skin-enabled! true)
(metal/set-onion-skin-prev-count! 2)
(metal/set-onion-skin-next-count! 1)
(metal/set-onion-skin-opacity! 0.4)
```

---

## Visual Design Options

### Classic Onion Skin (Recommended)
- Previous frames: Red/orange tint
- Next frames: Blue/green tint
- Opacity: 20-40%
- Count: 1-3 frames each direction

### Grayscale Ghost
- All frames: Grayscale with no tint
- Opacity falls off with distance
- Cleaner look, less distracting

### Outline Only (Advanced)
- Extract edges from frames
- Show only outlines
- Requires edge detection shader (more complex)

---

## Memory Impact

**Good news**: This implementation uses the **existing frame texture cache** - no additional GPU memory needed!

| Component | Memory | Already Allocated? |
|-----------|--------|-------------------|
| Frame textures | 12 × 33 MB = 396 MB | ✅ Yes |
| Onion skin pipeline | ~1 KB | New (negligible) |
| Uniforms | ~80 bytes | New (negligible) |

**Total new memory: ~1 KB** (just the pipeline state)

---

## Performance Considerations

1. **GPU-bound**: All compositing happens on GPU
2. **No CPU copies**: Uses existing cached textures
3. **Render passes**: Adds 1-6 render passes per frame (depending on onion count)
4. **Should be smooth**: Each pass is just a textured quad draw

### Optimization Tips

- Use `MTLLoadActionLoad` to preserve previous content
- Batch multiple frames into single command buffer
- Consider using a single render pass with multiple draw calls

---

## Files to Modify

| File | Changes |
|------|---------|
| `stamp_shaders.metal` | Add `onion_skin_vertex` and `onion_skin_fragment` shaders |
| `metal_renderer.mm` | Add onion skin state, pipeline, composite method, modify `present()` |
| `metal_renderer.h` | Add C API function declarations |
| `drawing_mobile_ios.mm` | Add gesture or UI toggle |

---

## Implementation Order

1. **Phase 1: Core Shader** (~30 min)
   - Add onion skin shader to `stamp_shaders.metal`
   - Test compilation

2. **Phase 2: Pipeline & State** (~30 min)
   - Add properties to impl class
   - Create pipeline in init
   - Set default values

3. **Phase 3: Composite Function** (~45 min)
   - Implement `drawOnionSkinFramesToTexture:`
   - Modify `present()` to call it
   - Test with hardcoded enabled=true

4. **Phase 4: C API** (~15 min)
   - Add exported functions
   - Test from REPL

5. **Phase 5: UI Integration** (~30 min)
   - Add gesture or button toggle
   - Polish and test

**Total estimated time: ~2.5 hours**

---

## Testing Checklist

- [ ] Onion skin renders previous frames with red tint
- [ ] Onion skin renders next frames with blue tint
- [ ] Opacity correctly falls off with distance
- [ ] Toggle on/off works
- [ ] Works with pan/zoom/rotate transforms
- [ ] No performance degradation when disabled
- [ ] Memory usage doesn't increase significantly
- [ ] Works correctly when frames loop (frame 0 shows frame 11 as previous)
- [ ] Handles empty/uncached frames gracefully

---

## Alternative Approaches Considered

### Stroke Replay (Rejected)
Re-render strokes from other frames with tinting. Problems:
- Much slower (must replay all strokes)
- Complex (need stroke data for all frames)
- Already have cached textures available

### CPU Compositing (Rejected)
Composite frames on CPU, upload to GPU. Problems:
- Slow (CPU-GPU transfer)
- High memory (additional CPU buffers)
- Frame drops on heavy scenes

### Separate Onion Layer (Rejected)
Maintain separate texture for onion skin. Problems:
- Additional 33 MB per layer
- Extra complexity
- No real benefit over direct compositing

---

## References

- Procreate onion skin: Shows 3 frames before/after, color-coded
- Looom: Shows frames in a fan/wheel arrangement
- Traditional animation: Uses colored pencil (blue for future, red for past)
