# Brush Selection Fix - January 6, 2026

## Summary
Fixed brush selection in iOS drawing app - selecting different brushes from the picker now properly affects stroke appearance.

## Root Causes Identified

### 1. Hardcoded Brush Settings Override
**Location**: `drawing_mobile_ios.mm:1333-1355`

The initialization order was wrong:
```cpp
// WRONG: applyBrush called first, then immediately overwritten
loadBrushesFromBundledFile();  // calls applyBrush internally
metal_stamp_set_brush_type(1);  // OVERWRITES the brush!
metal_stamp_set_brush_hardness(0.35f);
// ... etc
```

**Fix**: Set defaults BEFORE loading brushes:
```cpp
// Set defaults first
metal_stamp_set_brush_type(1);
metal_stamp_set_brush_hardness(0.35f);
// ...

// Load brushes - applyBrush will OVERRIDE defaults
loadBrushesFromBundledFile();
```

### 2. Missing Brush Type in applyBrush
**Location**: `brush_importer.mm:648-674`

The `applyBrush` method set spacing, jitter, textures, etc. but NOT the brush type.

**Fix**: Added brush type setting:
```cpp
+ (BOOL)applyBrush:(int32_t)brushId {
    ImportedBrush* brush = [self getBrushById:brushId];
    if (!brush) return NO;

    // Determine brush type based on textures
    int brushType = (brush->shapeTextureId >= 0 || brush->grainTextureId >= 0) ? 1 : 0;
    metal_stamp_set_brush_type(brushType);
    // ... rest of settings
}
```

### 3. stampTexturePipeline Never Created
**Location**: `metal_renderer.mm`

The `stampTexturePipeline` was declared but never created. The code fell back to `stamp_fragment_crayon` which uses PROCEDURAL noise, not imported textures.

**Fix**: Created the pipeline with `stamp_textured_fragment` shader:
```cpp
id<MTLFunction> texturedFragmentFunc = [library newFunctionWithName:@"stamp_textured_fragment"];
if (texturedFragmentFunc) {
    pipelineDesc.fragmentFunction = texturedFragmentFunc;
    self.stampTexturePipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
}
```

### 4. CRITICAL: Wrong Texture Channel Sampling
**Location**: `stamp_shaders.metal`

Shape textures are loaded as `MTLPixelFormatR8Unorm` (single RED channel grayscale).

**The Bug**: Shader was sampling `.a` (alpha) which is always 1.0 for R8 textures!
```metal
// WRONG
out_color.a *= texColor.a * uniforms.opacity;  // .a is always 1.0!
```

**The Fix**: Sample `.r` (red channel) instead:
```metal
// CORRECT
float shapeAlpha = texColor.r;  // Use red channel for R8 textures
out_color.a *= shapeAlpha * uniforms.opacity;
```

### 5. CGBitmapContext Inverts Grayscale Values
**Location**: `brush_importer.mm` texture loading + `stamp_shaders.metal`

When loading textures using CGBitmapContext with grayscale colorspace, the values get INVERTED during the draw operation.

**The Problem**: Source PNG has BLACK background (0) with WHITE logo (255). After loading, shader samples show HIGH values (≈1.0) where source was BLACK.

**Debug process**:
1. Created debug shader outputting raw R value as red: `return half4(texColor.r, 0.0, 0.0, 1.0)`
2. Observed RED everywhere (high R values) when source had BLACK background
3. Confirmed by testing inversion: `float inverted = 1.0 - texColor.r;` showed correct pattern

**The Fix**: Invert the texture value in the shader:
```metal
fragment half4 stamp_textured_fragment(...) {
    float4 texColor = brushTexture.sample(brushSampler, pointCoord);

    // INVERT - CGBitmapContext grayscale loading inverts values
    float shapeAlpha = 1.0 - texColor.r;

    if (shapeAlpha < 0.01) {
        discard_fragment();
    }

    float4 out_color = in.color;
    out_color.a *= shapeAlpha * uniforms.opacity * uniforms.flow;
    return half4(out_color);
}
```

## Key Technical Insights

### R8 Texture Format
For `MTLPixelFormatR8Unorm` textures:
- `.r` = the actual grayscale value (0.0-1.0)
- `.g` = 0.0
- `.b` = 0.0
- `.a` = 1.0 (always!)

### Procreate Brush Shape Convention
- WHITE (1.0) = fully opaque (paint here)
- BLACK (0.0) = fully transparent (no paint)

### CGBitmapContext Grayscale Gotcha
When using `CGBitmapContextCreate` with `CGColorSpaceCreateDeviceGray()` and drawing an image, the grayscale values get INVERTED. This is a known quirk - always verify texture data visually or with debug shaders!

## Files Modified

1. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Reordered initialization: defaults → brush loading → user settings

2. **`DrawingMobile/brush_importer.mm`**
   - Added extern declaration for `metal_stamp_set_brush_type`
   - Added brush type setting in `applyBrush`

3. **`src/vybe/app/drawing/native/metal_renderer.mm`**
   - Created `stampTexturePipeline` with `stamp_textured_fragment`
   - Modified pipeline selection to use textured pipeline when shape texture exists

4. **`src/vybe/app/drawing/native/stamp_shaders.metal`**
   - Fixed `stamp_textured_fragment` to sample RED channel instead of alpha

## Commands

```bash
# Full rebuild (required for .metal shader changes)
make drawing-ios-jit-sim-run

# Incremental build (for .mm changes only)
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' build

xcrun simctl terminate 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
xcrun simctl install 'iPad Pro 13-inch (M4)' ~/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-*/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

## Verification

Tested successfully:
- Multiple distinct brush textures visible on canvas
- "12 Brushes of Christmas" text pattern brush works
- Scattered/dotted brushes work
- Solid brushes work
- Different brushes produce visually distinct strokes

## What's Next

- Consider adding more brush types/modes
- Implement grain texture support (currently only shape texture)
- Add brush preview in picker showing actual stroke sample
