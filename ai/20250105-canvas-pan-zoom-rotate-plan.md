# Canvas Pan/Zoom/Rotate Implementation Plan

**Date**: 2025-01-05

## Goal

Implement Procreate-style canvas interaction:
- **Apple Pencil**: Draws on the canvas
- **Fingers**: Pan, zoom, and rotate the canvas

## Research Summary

### Procreate Gesture Behavior
- Two-finger pinch to zoom in/out
- Two-finger twist to rotate canvas
- Two-finger drag to pan canvas
- Apple Pencil always draws (never pans/zooms)
- Palm rejection built into Apple Pencil hardware

Sources:
- [Procreate Gestures Handbook](https://help.procreate.com/procreate/handbook/interface-gestures/gestures)
- [Procreate Apple Pencil Support](https://help.procreate.com/procreate/handbook/interface-gestures/pencil)

### Gesture Algorithm

The unified gesture model uses **two finger positions** to derive all transformations simultaneously:

```
// Track start and current positions of two fingers
struct TwoFingerGesture {
    float2 finger0_start, finger0_current;
    float2 finger1_start, finger1_current;
    bool active;
};

// Calculate rotation (radians)
float2 startVec = finger0_start - finger1_start;
float2 currVec = finger0_current - finger1_current;
float rotation = atan2(currVec.y, currVec.x) - atan2(startVec.y, startVec.x);

// Calculate scale (ratio)
float scale = length(currVec) / length(startVec);

// Calculate pan (center point displacement)
float2 startCenter = (finger0_start + finger1_start) / 2;
float2 currCenter = (finger0_current + finger1_current) / 2;
float2 pan = currCenter - startCenter;
```

Source: [Pan/Zoom/Rotate Gesture Model](https://mortoray.com/a-pan-zoom-and-rotate-gesture-model-for-touch-devices/)

### SDL3 Input Handling

SDL3 provides separate events for pen and touch:
- **Pen events**: `SDL_EVENT_PEN_DOWN`, `SDL_EVENT_PEN_MOTION`, `SDL_EVENT_PEN_UP`
- **Finger events**: `SDL_EVENT_FINGER_DOWN`, `SDL_EVENT_FINGER_MOTION`, `SDL_EVENT_FINGER_UP`

Key difference: Pen events are for Apple Pencil (drawing), finger events for touch gestures.

## Implementation Architecture

### 1. Canvas Transform State

```cpp
struct CanvasTransform {
    float2 pan;          // Pan offset in screen pixels
    float scale;         // Zoom level (1.0 = 100%)
    float rotation;      // Rotation in radians
    float2 pivot;        // Transform pivot point (screen center)

    // Accumulated values during gesture
    float2 gesturePanStart;
    float gestureScaleStart;
    float gestureRotationStart;
};
```

### 2. Gesture Tracking State

```cpp
struct GestureState {
    bool isActive;                    // True if two fingers are down
    int finger0_id, finger1_id;       // SDL finger IDs
    float2 finger0_start, finger1_start;  // Starting positions
    float2 finger0_current, finger1_current;  // Current positions

    // Accumulated transform at gesture start
    float2 basePan;
    float baseScale;
    float baseRotation;
};
```

### 3. Coordinate Transformation

To draw at the correct canvas position when canvas is transformed:

```cpp
// Screen coords -> Canvas coords (for drawing)
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

// Canvas coords -> NDC (for rendering)
// Apply transform.pan, transform.scale, transform.rotation in Metal shader
```

### 4. Modified Rendering Pipeline

Option A: **Transform the canvas texture when rendering**
- Draw strokes to canvas at original coordinates
- Apply pan/zoom/rotate when blitting canvas to screen
- Simpler, but limited by texture resolution at high zoom

Option B: **Transform stroke coordinates before drawing**
- Convert screen touch coords to canvas coords
- Draw strokes at transformed positions
- More complex, but maintains quality at any zoom

**Recommended**: Option A for simplicity, with Option B refinement later.

### 5. Metal Shader Changes

Add canvas transform uniforms:

```metal
struct CanvasUniforms {
    float2 pan;
    float scale;
    float rotation;
    float2 pivot;
};

vertex float4 canvas_blit_vertex(
    uint vid [[vertex_id]],
    constant CanvasUniforms& canvas [[buffer(0)]]
) {
    // Apply transform to quad vertices
    float2 pos = corners[vid];

    // Translate to pivot
    pos -= canvas.pivot;

    // Apply scale
    pos *= canvas.scale;

    // Apply rotation
    float c = cos(canvas.rotation);
    float s = sin(canvas.rotation);
    pos = float2(pos.x * c - pos.y * s, pos.x * s + pos.y * c);

    // Apply pan and translate back
    pos += canvas.pivot + canvas.pan;

    // Convert to NDC
    return float4(pos * 2.0 - 1.0, 0.0, 1.0);
}
```

## Implementation Steps

### Phase 1: Gesture Detection (finger events)

1. **Track finger down events**
   - When first finger down: record finger0_id and position
   - When second finger down: record finger1_id and position, start gesture
   - Store current canvas transform as gesture baseline

2. **Track finger motion**
   - If gesture active (two fingers): update current positions
   - Calculate delta rotation, scale, pan from start positions
   - Apply deltas to baseline transform

3. **Track finger up events**
   - When any finger lifts: end gesture
   - Commit current transform as new baseline

### Phase 2: Transform Application

1. **Add CanvasTransform to renderer state**
2. **Modify present() to apply transform when blitting**
3. **Add uniforms to Metal blit shader**

### Phase 3: Coordinate Conversion for Drawing

1. **When pen down/motion: convert screen coords to canvas coords**
2. **Draw strokes at canvas coordinates**
3. **Ensures drawing appears correct regardless of zoom/pan/rotate**

### Phase 4: UI Handling

1. **UI elements (sliders, color picker) stay in screen space**
2. **Check UI hit areas before checking canvas gestures**
3. **UI not affected by canvas transform**

## File Changes Required

| File | Changes |
|------|---------|
| `metal_renderer.h` | Add CanvasTransform struct, transform methods |
| `metal_renderer.mm` | Add canvas transform uniforms, modify blit shader |
| `stamp_shaders.metal` | Add canvas_blit_vertex with transform |
| `drawing_mobile_ios.mm` | Add GestureState, gesture handling in finger events |

## Edge Cases to Handle

1. **Zoom limits**: Clamp scale between 0.1x and 10x
2. **Rotation snapping**: Optional snap to 0/90/180/270 degrees
3. **Double-tap to reset**: Reset transform to default
4. **Fit to screen**: Button to reset view
5. **Single finger pan**: Optional mode for one-finger panning

## Testing Plan

1. Two-finger pinch zoom - canvas should zoom around finger center
2. Two-finger rotate - canvas should rotate around finger center
3. Two-finger pan - canvas should follow finger movement
4. Draw with Apple Pencil while zoomed - stroke appears at correct position
5. Draw while rotated - stroke orientation correct
6. UI interaction - sliders/picker work regardless of canvas transform

## References

- [Procreate Gestures](https://help.procreate.com/procreate/handbook/interface-gestures/gestures)
- [Pan/Zoom/Rotate Algorithm](https://mortoray.com/a-pan-zoom-and-rotate-gesture-model-for-touch-devices/)
- [SDL_gesture library for SDL3](https://github.com/libsdl-org/SDL_gesture)
- [iOS Gesture Recognizers](https://www.appcoda.com/ios-gesture-recognizers/)
- [CodePath Gesture Tutorial](https://guides.codepath.com/ios/Moving-and-Transforming-Views-with-Gestures)
