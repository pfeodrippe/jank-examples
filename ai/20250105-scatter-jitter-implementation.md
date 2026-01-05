# Scatter, Jitter, and Complete Brush System - Metal Renderer

**Date**: 2025-01-05

## What I Learned

### 1. Scatter Implementation (Perpendicular Offset)

Scatter adds random offset perpendicular to the stroke direction for organic feel:

```objc
// In interpolateFrom:to: method
// Calculate perpendicular direction to stroke
float perpX = (strokeLen > 0.001f) ? -dy / strokeLen : 0.0f;
float perpY = (strokeLen > 0.001f) ? dx / strokeLen : 0.0f;

// Apply scatter in pixels
if (scatter > 0.0f) {
    float scatterAmount = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * scatter;
    float scatterNDC = scatterAmount * size / self.width;  // Convert to NDC
    pos.x += perpX * scatterNDC;
    pos.y += perpY * scatterNDC;
}
```

### 2. Size and Opacity Jitter

Random variations applied per-stamp for organic look:

```objc
// Size jitter: varies stamp size by percentage
if (sizeJitter > 0.0f) {
    float jitter = 1.0f + ((float)rand() / RAND_MAX - 0.5f) * 2.0f * sizeJitter;
    finalSize *= jitter;  // e.g., 0.15 = +/- 15% size variation
}

// Opacity jitter: randomly reduces opacity
if (opacityJitter > 0.0f) {
    float jitter = 1.0f - ((float)rand() / RAND_MAX) * opacityJitter;
    finalColor.w *= jitter;  // e.g., 0.1 = up to 10% opacity reduction
}
```

### 3. Pressure Dynamics Formula

Pressure affects size/opacity based on configurable sensitivity:

```cpp
// Formula: When pressure=0, effect is reduced; when pressure=1, full effect
float sizeFactor = 1.0f - brush_.size_pressure + (brush_.size_pressure * pressure);
float opacityFactor = 1.0f - brush_.opacity_pressure + (brush_.opacity_pressure * pressure);

// Example: size_pressure=0.8, pressure=0.5
// sizeFactor = 1.0 - 0.8 + (0.8 * 0.5) = 0.2 + 0.4 = 0.6 (60% of base size)
```

### 4. Complete Huntsman Crayon Settings

```cpp
// Brush type and basic settings
metal_stamp_set_brush_type(1);           // Crayon shader
metal_stamp_set_brush_size(50.0f);       // Base size in pixels
metal_stamp_set_brush_hardness(0.35f);   // Soft-medium edge
metal_stamp_set_brush_opacity(0.9f);
metal_stamp_set_brush_spacing(0.06f);    // Tight spacing for smooth strokes
metal_stamp_set_brush_grain_scale(1.8f); // Visible paper grain
metal_stamp_set_brush_color(0.15f, 0.45f, 0.75f, 1.0f);

// Pressure dynamics
metal_stamp_set_brush_size_pressure(0.8f);    // Strong size response
metal_stamp_set_brush_opacity_pressure(0.3f); // Subtle opacity response

// Scatter and jitter
metal_stamp_set_brush_scatter(3.0f);          // Pixels of perpendicular offset
metal_stamp_set_brush_size_jitter(0.15f);     // 15% size variation
metal_stamp_set_brush_opacity_jitter(0.1f);   // 10% opacity variation
```

### 5. Method Signature Updates

When adding new parameters to Objective-C methods, update all call sites:

```objc
// Before (old signature)
- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing;

// After (new signature with scatter/jitter)
- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing
                scatter:(float)scatter sizeJitter:(float)sizeJitter opacityJitter:(float)opacityJitter;
```

### 6. Adding New API Functions Pattern

For each new brush setting, add in 3 places:

1. **Class method** in `MetalStampRenderer`:
```cpp
void MetalStampRenderer::set_brush_size_jitter(float amount) {
    brush_.size_jitter = amount;
}
```

2. **Namespace function** in `metal_stamp` namespace:
```cpp
void metal_set_brush_size_jitter(float amount) {
    if (g_metal_renderer) g_metal_renderer->set_brush_size_jitter(amount);
}
```

3. **extern "C" wrapper** for JIT linking:
```cpp
METAL_EXPORT void metal_stamp_set_brush_size_jitter(float amount) {
    metal_stamp::metal_set_brush_size_jitter(amount);
}
```

## Files Modified

| File | Changes |
|------|---------|
| `metal_renderer.h` | Added `set_brush_size_jitter()`, `set_brush_opacity_jitter()` methods and extern "C" wrappers |
| `metal_renderer.mm` | Implemented jitter setters, updated `interpolateFrom:to:` with scatter/jitter, pressure dynamics in `begin_stroke`/`add_stroke_point` |
| `drawing_mobile_ios.mm` | Added scatter and jitter settings for Huntsman crayon brush |

## Commands Run

```bash
# Sync shader files
cp src/vybe/app/drawing/native/stamp_shaders.metal DrawingMobile/jank-resources/src/jank/vybe/app/drawing/native/

# Build iOS simulator app
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/ios_build.txt

# Direct install (when makefile install fails)
xcrun simctl install 'iPad Pro 13-inch (M4)' "/path/to/DrawingMobile-JIT-Sim.app"
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

## What's Next

- Rotation jitter (random stamp rotation)
- Tilt support (Apple Pencil tilt affects brush shape)
- Image-based brush textures (loading PNG textures vs procedural)
- Undo/redo system
- Multiple brush presets UI
