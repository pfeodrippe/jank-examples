# Pen Coordinate Fix - iOS SDL3

## Issue

When using Apple Pencil on iOS device, strokes were drawn as a line from the origin to the actual touch point. Finger input worked correctly.

## Root Cause

SDL3 pen events (`SDL_EVENT_PEN_DOWN`, `SDL_EVENT_PEN_MOTION`) use **window coordinates in points** (logical pixels), NOT physical pixels.

On iOS retina displays:
- `SDL_GetWindowSizeInPixels()` returns physical pixels (e.g., 2778 x 1284)
- Pen events return coordinates in **points** (e.g., 926 x 428 on 3x display)
- Finger events are normalized (0-1) and multiplied by pixel dimensions

**FINGER events (correct):**
```cpp
float x = event.tfinger.x * width;   // Normalized (0-1) * pixels = pixels
float y = event.tfinger.y * height;
```

**PEN events (was broken):**
```cpp
float screenX = event.ptouch.x;      // Points, not pixels!
float screenY = event.ptouch.y;
```

Since pen coordinates were ~3x smaller than expected (points vs pixels on 3x retina), strokes appeared compressed toward the origin.

## Solution

Use `SDL_GetWindowPixelDensity()` to scale pen coordinates from points to physical pixels:

```cpp
// At initialization
float pixelDensity = SDL_GetWindowPixelDensity(window);

// SDL_EVENT_PEN_DOWN
float screenX = event.ptouch.x * pixelDensity;
float screenY = event.ptouch.y * pixelDensity;

// SDL_EVENT_PEN_MOTION
float screenX = event.pmotion.x * pixelDensity;
float screenY = event.pmotion.y * pixelDensity;
```

## Files Modified

- `DrawingMobile/drawing_mobile_ios.mm`
  - Lines 1372-1375: Added `pixelDensity` from `SDL_GetWindowPixelDensity()`
  - Lines 2015-2017: Fixed `SDL_EVENT_PEN_DOWN` - multiply by pixelDensity
  - Lines 2111-2114: Fixed `SDL_EVENT_PEN_MOTION` - multiply by pixelDensity

## Key Learning

SDL3 coordinate systems on iOS:
- **Touch/finger events**: Normalized (0-1), multiply by pixel dimensions
- **Pen events**: Window coordinates in **points**, multiply by `pixelDensity`
- **Mouse events**: Also window coordinates in points

The `pixelDensity` is typically 2.0 or 3.0 on iOS retina displays.

## Related Session

See `ai/20260107-brush-fixes-session2.md` for the brush picker fixes done earlier in this session.
