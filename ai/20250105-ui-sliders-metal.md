# UI Sliders with Metal Rendering

**Date**: 2025-01-05

## What I Learned

### 1. Metal UI Rect Pipeline

Created a complete UI rendering pipeline separate from brush rendering:

```metal
// UI Rect shader - SDF rounded rectangle
struct UIRectParams {
    float4 rect;       // x, y, width, height in NDC
    float4 color;
    float cornerRadius;
};

vertex UIVertexOut ui_rect_vertex(uint vid [[vertex_id]], constant UIRectParams& params [[buffer(0)]]) {
    float2 corners[4] = { float2(0,0), float2(1,0), float2(0,1), float2(1,1) };
    float2 pos = params.rect.xy + corners[vid] * params.rect.zw;
    // ...
}

fragment half4 ui_rect_fragment(UIVertexOut in [[stage_in]], constant UIRectParams& params [[buffer(0)]]) {
    // SDF rounded rectangle with antialiased edges
    float r = params.cornerRadius / min(size.x, size.y);
    float2 q = abs(p) - (float2(0.5) - r);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float aa = fwidth(d) * 1.5;
    float alpha = 1.0 - smoothstep(-aa, aa, d);
    // ...
}
```

### 2. UI Rect Queue System

UI rects are queued during the frame and drawn after the canvas blit:

```objc
// Queue a rect (screen coords to NDC conversion)
- (void)queueUIRect:(float)x y:(float)y width:(float)w height:(float)h
              color:(simd_float4)color cornerRadius:(float)radius {
    float ndcX = (x / self.drawableWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / self.drawableHeight) * 2.0f;  // Flip Y
    // ...
    _uiRects.push_back(params);
}

// Draw all queued rects to texture
- (void)drawQueuedUIRectsToTexture:(id<MTLTexture>)texture {
    for (const UIRectParams& params : _uiRects) {
        [encoder setVertexBytes:&params length:sizeof(UIRectParams) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
}
```

### 3. Modified Present Flow

```cpp
void MetalStampRenderer::present() {
    // 1. Blit canvas to drawable
    [blitEncoder copyFromTexture:impl_.canvasTexture ...];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    // 2. Draw UI overlays on drawable
    [impl_ drawQueuedUIRectsToTexture:drawable.texture];
    [impl_ clearUIQueue];

    // 3. Present
    [presentBuffer presentDrawable:drawable];
}
```

### 4. Slider State Management

```cpp
struct SliderConfig {
    float x, y;           // Position (top-left)
    float width, height;  // Size
    float value;          // Current value (0.0 - 1.0)
    float minVal, maxVal; // Actual value range
    bool isDragging;
};

// Touch handling with expanded hit area
float sliderHitPadding = 30.0f;
if (isPointInSlider(expandedSize, x, y)) {
    sizeSlider.isDragging = true;
    float relY = (y - sizeSlider.y) / sizeSlider.height;
    sizeSlider.value = 1.0f - std::max(0.0f, std::min(1.0f, relY));
    brushSize = sizeSlider.minVal + sizeSlider.value * (sizeSlider.maxVal - sizeSlider.minVal);
    metal_stamp_set_brush_size(brushSize);
}
```

### 5. Procreate-Style Vertical Slider Drawing

```cpp
static void drawVerticalSlider(const SliderConfig& slider, ...) {
    // Track background (dark semi-transparent)
    metal_stamp_queue_ui_rect(slider.x, slider.y, slider.width, slider.height,
                              0.2f, 0.2f, 0.2f, 0.7f, 10.0f);

    // Filled portion (shows current value)
    float fillHeight = slider.height * slider.value;
    float fillY = slider.y + slider.height - fillHeight;
    metal_stamp_queue_ui_rect(slider.x, fillY, slider.width, fillHeight,
                              r, g, b, 0.9f, 10.0f);

    // Knob/handle (white circle)
    float knobSize = slider.width * 1.3f;
    metal_stamp_queue_ui_rect(knobX, knobY, knobSize, knobSize,
                              1.0f, 1.0f, 1.0f, 1.0f, knobSize / 2.0f);
}
```

### 6. External vs Embedded Shaders

UI shaders must be in the external .metal file, not just the fallback:
- External `.metal` file is compiled by Xcode into `.metallib`
- Fallback embedded shaders only used when `.metallib` fails to load
- Both need the UI rect shaders for cross-platform support

## Files Modified

| File | Changes |
|------|---------|
| `metal_renderer.h` | Added `queue_ui_rect()` method and `metal_stamp_queue_ui_rect` extern "C" |
| `metal_renderer.mm` | Added UIRectParams struct, uiRectPipeline, queueUIRect, drawQueuedUIRectsToTexture methods, modified present() |
| `stamp_shaders.metal` | Added ui_rect_vertex and ui_rect_fragment shaders |
| `drawing_mobile_ios.mm` | Added SliderConfig struct, drawVerticalSlider function, slider touch handling |

## Commands Run

```bash
# Sync shader to iOS resources
cp src/vybe/app/drawing/native/stamp_shaders.metal \
   DrawingMobile/jank-resources/src/jank/vybe/app/drawing/native/

# Build and run (install often fails, use direct install)
make drawing-ios-jit-sim-run

# Direct install and launch (more reliable)
xcrun simctl install 'iPad Pro 13-inch (M4)' "/path/to/DrawingMobile-JIT-Sim.app"
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

### 7. Apple Pencil Pressure via SDL3 Pen API

SDL3 has a dedicated Pen API for stylus input (added in 3.1.3/3.2.0). Regular touch events (`SDL_EVENT_FINGER_*`) don't properly expose Apple Pencil pressure - must use pen events:

```cpp
// Track pressure from axis events
float pen_pressure = 1.0f;

case SDL_EVENT_PEN_AXIS: {
    if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
        pen_pressure = event.paxis.value;  // 0.0 to 1.0
    }
    break;
}

case SDL_EVENT_PEN_DOWN: {
    float x = event.ptouch.x;  // Already in screen pixels
    float y = event.ptouch.y;
    metal_stamp_begin_stroke(x, y, pen_pressure);
    break;
}

case SDL_EVENT_PEN_MOTION: {
    float x = event.pmotion.x;
    float y = event.pmotion.y;
    metal_stamp_add_stroke_point(x, y, pen_pressure);
    break;
}

case SDL_EVENT_PEN_UP: {
    metal_stamp_end_stroke();
    pen_pressure = 1.0f;  // Reset
    break;
}
```

Key differences from finger events:
- Pen events use `event.ptouch`, `event.pmotion`, `event.paxis` (not `event.tfinger`)
- Position already in screen pixels (not normalized 0-1 like finger events)
- Pressure comes from separate `SDL_EVENT_PEN_AXIS` events with `SDL_PEN_AXIS_PRESSURE`

References:
- [SDL3 Pen API](https://wiki.libsdl.org/SDL3/CategoryPen)
- [Apple Pencil PR #11753](https://github.com/libsdl-org/SDL/pull/11753)

### 8. Color Picker Button Implementation

Added a color button that cycles through 10 preset colors on tap:

```cpp
struct ColorPreset {
    float r, g, b;
    const char* name;
};

static const ColorPreset COLOR_PRESETS[] = {
    {0.15f, 0.45f, 0.75f, "Blue"},
    {0.85f, 0.25f, 0.25f, "Red"},
    {0.25f, 0.75f, 0.35f, "Green"},
    // ... 10 colors total
};

struct ColorButtonConfig {
    float x, y;           // Position (top-left)
    float size;           // Diameter
    int currentColorIndex;
};

// Draw color button (white ring with colored center)
static void drawColorButton(const ColorButtonConfig& btn) {
    const ColorPreset& color = COLOR_PRESETS[btn.currentColorIndex];

    // Outer ring (white)
    metal_stamp_queue_ui_rect(btn.x, btn.y, btn.size, btn.size,
                              1.0f, 1.0f, 1.0f, 1.0f, btn.size / 2.0f);

    // Inner color circle
    float innerMargin = 4.0f;
    float innerSize = btn.size - innerMargin * 2;
    metal_stamp_queue_ui_rect(btn.x + innerMargin, btn.y + innerMargin,
                              innerSize, innerSize,
                              color.r, color.g, color.b, 1.0f, innerSize / 2.0f);
}

// Touch handling - cycle colors
if (isPointInColorButton(colorButton, x, y)) {
    colorButton.currentColorIndex = (colorButton.currentColorIndex + 1) % NUM_COLOR_PRESETS;
    const ColorPreset& newColor = COLOR_PRESETS[colorButton.currentColorIndex];
    metal_stamp_set_brush_color(newColor.r, newColor.g, newColor.b, 1.0f);
}
```

## Current Status

- UI rendering pipeline: Working
- Sliders visible: Yes (at bottom of screen in portrait mode)
- Slider touch interaction: Working (brush size and opacity change)
- Color button: Implemented and rendering
- Apple Pencil pressure: SDL3 pen event handlers added
- Crayon brush: Working beautifully with texture

## Files Modified (Updated)

| File | Changes |
|------|---------|
| `metal_renderer.h` | Added `queue_ui_rect()` method and `metal_stamp_queue_ui_rect` extern "C" |
| `metal_renderer.mm` | Added UIRectParams struct, uiRectPipeline, queueUIRect, drawQueuedUIRectsToTexture methods |
| `stamp_shaders.metal` | Added ui_rect_vertex and ui_rect_fragment shaders |
| `drawing_mobile_ios.mm` | Added SliderConfig, ColorButtonConfig, ColorPreset, SDL3 pen event handlers, drawVerticalSlider, drawColorButton |

## What's Next

1. Fix UI positioning - sliders/button appear at bottom instead of left edge in portrait mode
2. Test Apple Pencil pressure on real device (simulator doesn't have pressure)
3. Add undo/redo buttons
4. Consider full color picker modal (HSB wheel) instead of just presets
