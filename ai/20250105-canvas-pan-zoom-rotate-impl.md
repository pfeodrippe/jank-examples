# Canvas Pan/Zoom/Rotate Implementation

**Date**: 2025-01-05

## Summary

Implemented Procreate-style canvas interaction where:
- **Apple Pencil**: Draws on the canvas (pen events)
- **Fingers**: Pan, zoom, and rotate the canvas (finger events)

## What Was Implemented

### Phase 1: Two-Finger Gesture Detection

Added gesture tracking in `drawing_mobile_ios.mm`:

```cpp
// Canvas transform state
struct CanvasTransform {
    float panX, panY;      // Pan offset in screen pixels
    float scale;           // Zoom level (1.0 = 100%)
    float rotation;        // Rotation in radians
    float pivotX, pivotY;  // Transform pivot (screen center)
};

// Two-finger gesture tracking
struct TwoFingerGesture {
    bool isActive;
    SDL_FingerID finger0_id, finger1_id;
    float finger0_startX, finger0_startY;  // Normalized 0-1
    float finger1_startX, finger1_startY;
    float finger0_currX, finger0_currY;
    float finger1_currX, finger1_currY;
    // Baseline transform at gesture start
    float basePanX, basePanY;
    float baseScale;
    float baseRotation;
};
```

**Gesture Algorithm**:
- Scale: Ratio of current finger distance to start distance
- Rotation: Difference in angles between finger vectors
- Pan: Displacement of the center point between fingers

### Phase 2: Metal Canvas Transform Rendering

Added canvas blit shader with transform in `stamp_shaders.metal`:

```metal
struct CanvasTransformUniforms {
    float2 pan;           // Pan offset in NDC
    float scale;          // Zoom level
    float rotation;       // Rotation in radians
    float2 pivot;         // Transform pivot in NDC
    float2 viewportSize;  // Viewport size
};

vertex CanvasBlitVertexOut canvas_blit_vertex(...) {
    // Apply inverse transform to UV coordinates
    // 1. Translate to pivot
    // 2. Undo scale
    // 3. Undo rotation
    // 4. Undo pan
    // 5. Translate back from pivot
}
```

**Renderer Changes** (`metal_renderer.mm`):
- Added `CanvasTransformUniforms` struct
- Added `canvasBlitPipeline` for transformed canvas rendering
- Added `setCanvasTransformPanX:panY:scale:rotation:pivotX:pivotY:` method
- Added `drawCanvasToTexture:` method using the transform shader
- Modified `present()` to use transformed draw when transform is active

**New extern "C" Function**:
```cpp
void metal_stamp_set_canvas_transform(float panX, float panY, float scale,
                                      float rotation, float pivotX, float pivotY);
```

### Also Implemented: Color Picker Panel

Added a grid-based color picker (6x5 grid = 30 colors):
- HSV to RGB conversion for color generation
- Bottom row is grayscale
- Tap button to open, tap color to select, tap outside to close

## Files Modified

| File | Changes |
|------|---------|
| `drawing_mobile_ios.mm` | CanvasTransform struct, TwoFingerGesture struct, gesture tracking in FINGER_DOWN/MOTION/UP events, color picker panel |
| `metal_renderer.h` | Added `set_canvas_transform()` method and `metal_stamp_set_canvas_transform` extern "C" |
| `metal_renderer.mm` | CanvasTransformUniforms, canvasBlitPipeline, setCanvasTransformPanX method, drawCanvasToTexture, modified present() |
| `stamp_shaders.metal` | Added `canvas_blit_vertex` and `canvas_blit_fragment` shaders |

## Commands Run

```bash
# Copy shader to iOS resources
cp src/vybe/app/drawing/native/stamp_shaders.metal \
   DrawingMobile/jank-resources/src/jank/vybe/app/drawing/native/

# Build and run
make drawing-ios-jit-sim-run

# Manual install (when find command finds stale app)
xcrun simctl install 'iPad Pro 13-inch (M4)' \
    "/Users/pfeodrippe/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-heliwfyrogwsonecxqwxrcidpmhl/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app"

# Launch
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

## What's Next - Phase 3: Coordinate Conversion for Drawing

When the canvas is zoomed/rotated/panned, drawing strokes need to be converted from screen coordinates to canvas coordinates. Currently:

1. Finger gestures work for pan/zoom/rotate (transform applied to canvas rendering)
2. Apple Pencil draws to canvas at screen coordinates

**Needed**: Convert pen coordinates to canvas coordinates before drawing:

```cpp
// Screen coords -> Canvas coords
float2 screenToCanvas(float2 screenPos, const CanvasTransform& transform) {
    // 1. Translate to pivot
    float2 p = screenPos - transform.pivot;

    // 2. Undo rotation
    float cosR = cos(-transform.rotation);
    float sinR = sin(-transform.rotation);
    p = float2(p.x * cosR - p.y * sinR, p.x * sinR + p.y * cosR);

    // 3. Undo scale
    p = p / transform.scale;

    // 4. Undo pan
    p = p - transform.pan;

    // 5. Translate back from pivot
    return p + transform.pivot;
}
```

This conversion needs to be applied in:
- `SDL_EVENT_PEN_DOWN` - for stroke start position
- `SDL_EVENT_PEN_MOTION` - for stroke point positions

## Testing Notes

- **Single finger drawing**: Works in simulator (replaced pen-only drawing for testing)
- **Two-finger gestures**: Hold **Option key** and drag to perform pinch/rotate in simulator
- The gray background shows when canvas is zoomed out (out-of-bounds area)
- Scale is clamped between 0.1x and 10x
- On real device with Apple Pencil: Pencil draws, fingers pan/zoom/rotate

## Phase 3 Complete: Single-Finger Drawing for Simulator

Since iOS Simulator doesn't support Apple Pencil emulation, implemented single-finger drawing:

1. **FINGER_DOWN**: First finger starts drawing (instead of waiting for pen event)
2. **FINGER_MOTION**: Adds stroke points when drawing (with screen-to-canvas conversion)
3. **FINGER_UP**: Ends stroke when finger lifts
4. **Second finger**: Cancels drawing and switches to two-finger gesture mode

This allows testing the full pan/zoom/rotate + drawing workflow in the simulator.

## References

- [Plan Document](./20250105-canvas-pan-zoom-rotate-plan.md)
- [Procreate Gestures](https://help.procreate.com/procreate/handbook/interface-gestures/gestures)
- [Pan/Zoom/Rotate Algorithm](https://mortoray.com/a-pan-zoom-and-rotate-gesture-model-for-touch-devices/)
