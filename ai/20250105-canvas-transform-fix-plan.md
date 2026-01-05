# Canvas Transform Fix Plan

**Date**: 2025-01-05

## Problems Identified

1. **Canvas size changes during rotation** - The aspect ratio fix I applied incorrectly scales the canvas
2. **Drawing appears in wrong location after transform** - The `screenToCanvas()` C++ function doesn't match the shader transformation

## Root Cause Analysis

### Problem 1: Incorrect Aspect Ratio Fix

The shader currently does:
```metal
p.y *= aspect;   // Before rotation
// ... rotation ...
p.y /= aspect;   // After rotation
```

This is WRONG because:
- It scales Y before rotation, then scales Y back after
- But after rotation, the Y component has contributions from BOTH original X and Y
- This causes the canvas to stretch/shrink during rotation

### Problem 2: Mismatched Transformations

The shader and C++ `screenToCanvas()` function must use **identical math**. Currently they don't match:

**Shader** (in NDC space -1 to 1):
1. Translate to pivot
2. Undo scale
3. Undo rotation (with broken aspect fix)
4. Undo pan (converting from pixels to NDC)
5. Translate back from pivot

**C++ screenToCanvas()** (in pixel space):
1. Translate to pivot
2. Undo rotation
3. Undo scale
4. Undo pan
5. Translate back from pivot

The order is different, and the aspect ratio handling doesn't match!

## Correct Solution

### Key Insight from Research

From [Mortoray's gesture model](https://mortoray.com/a-pan-zoom-and-rotate-gesture-model-for-touch-devices/):
- Calculate cumulative transforms from gesture start
- Translation exists in rotated/scaled space

From [The Book of Shaders](https://thebookofshaders.com/08/):
- Order matters: translate → transform → translate back
- Matrix multiplication is not commutative

From [Metal coordinate spaces](https://www.kodeco.com/books/metal-by-tutorials/v2.0/chapters/4-coordinate-spaces):
- Use inverse matrix to go from screen to world coordinates

### The Fix: Work in Pixel Space, Not NDC

The problem is mixing NDC and pixel space. **Solution: Do all transform math in pixel space, then convert to UV at the end.**

### Correct Transformation Order

For rendering (inverse transform on UVs):
1. Convert screen position to pixel space
2. Translate to pivot (pixel space)
3. Undo pan
4. Undo scale
5. Undo rotation
6. Translate back from pivot
7. Convert to UV coordinates

For input mapping (same inverse transform):
1. Start with screen position (pixel space)
2. Translate to pivot
3. Undo pan
4. Undo scale
5. Undo rotation
6. Translate back from pivot
7. Result is canvas position

**Both must be IDENTICAL.**

## Implementation Steps

### Step 1: Fix the Shader

```metal
vertex CanvasBlitVertexOut canvas_blit_vertex(...) {
    float2 pos = corners[vid];

    // Convert from NDC to pixel space
    float2 pixelPos = (pos + 1.0) * 0.5 * transform.viewportSize;

    // Apply inverse transform in pixel space
    // 1. Translate to pivot
    float2 p = pixelPos - float2(transform.pivot);

    // 2. Undo pan
    p = p - transform.pan;

    // 3. Undo scale
    p = p / transform.scale;

    // 4. Undo rotation
    float c = cos(-transform.rotation);
    float s = sin(-transform.rotation);
    p = float2(p.x * c - p.y * s, p.x * s + p.y * c);

    // 5. Translate back from pivot
    p = p + float2(transform.pivot);

    // Convert to UV (0-1 range)
    float2 uv = p / transform.viewportSize;
    uv.y = 1.0 - uv.y;  // Flip Y for texture

    CanvasBlitVertexOut out;
    out.position = float4(corners[vid], 0.0, 1.0);
    out.uv = uv;
    return out;
}
```

### Step 2: Fix the C++ screenToCanvas Function

```cpp
static void screenToCanvas(float screenX, float screenY,
                           const CanvasTransform& transform,
                           int screenWidth, int screenHeight,
                           float& canvasX, float& canvasY) {
    // Input is already in pixel space
    float x = screenX;
    float y = screenY;

    // 1. Translate to pivot
    x -= transform.pivotX;
    y -= transform.pivotY;

    // 2. Undo pan
    x -= transform.panX;
    y -= transform.panY;

    // 3. Undo scale
    x /= transform.scale;
    y /= transform.scale;

    // 4. Undo rotation
    float cosR = cosf(-transform.rotation);
    float sinR = sinf(-transform.rotation);
    float rx = x * cosR - y * sinR;
    float ry = x * sinR + y * cosR;

    // 5. Translate back from pivot
    canvasX = rx + transform.pivotX;
    canvasY = ry + transform.pivotY;
}
```

### Step 3: Ensure Pivot is Correct

The pivot should be the center of the screen in pixels:
```cpp
canvasTransform.pivotX = width / 2.0f;
canvasTransform.pivotY = height / 2.0f;
```

### Step 4: Update Uniforms to Pass Pivot in Pixels

Currently the shader receives pivot in NDC. Change to pixels:
```cpp
// In setCanvasTransformPanX method:
uniforms.pivot = simd_make_float2(pivotX, pivotY);  // Already in pixels
```

## Why This Works

1. **No aspect ratio issues**: All math is in pixel space where X and Y have the same scale
2. **Perfect matching**: Shader and C++ use identical formulas
3. **Correct pivot**: Rotation/scale happens around the pivot point
4. **Correct order**: pan → scale → rotation ensures proper behavior

## Testing

1. Draw some strokes
2. Rotate canvas - strokes should NOT stretch
3. Pan/zoom canvas - strokes should remain correctly positioned
4. Draw after transform - new strokes should appear at touch point

## Files to Modify

1. `src/vybe/app/drawing/native/stamp_shaders.metal` - Fix shader transform
2. `DrawingMobile/drawing_mobile_ios.mm` - Fix screenToCanvas function
3. `src/vybe/app/drawing/native/metal_renderer.mm` - Ensure uniforms are in pixels (verify)

## References

- [Mortoray's Pan/Zoom/Rotate Gesture Model](https://mortoray.com/a-pan-zoom-and-rotate-gesture-model-for-touch-devices/)
- [The Book of Shaders: 2D Matrices](https://thebookofshaders.com/08/)
- [Metal Coordinate Spaces](https://www.kodeco.com/books/metal-by-tutorials/v2.0/chapters/4-coordinate-spaces)
- [LearnOpenGL Transformations](https://learnopengl.com/Getting-started/Transformations)
