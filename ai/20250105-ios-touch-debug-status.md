# iOS Touch Debug Status

**Date**: 2025-01-05
**Status**: In Progress

## Current State

1. **Strokes ARE persisting** - Multiple strokes visible on canvas, they don't disappear
2. **Zigzag pattern still present** - User's manual drawing shows zigzag characteristics
3. **MCP swipes produce smooth lines** - But they converge incorrectly (coordinates off)

## What We've Tried

### Coordinate Conversion Approaches:
1. `SDL_ConvertEventToRenderCoordinates()` - Didn't work correctly
2. `x * display_scale` - Coordinates too small
3. `x * render_width` - Current approach, may be causing zigzag

### Current Code (touch handling):
```cpp
case SDL_EVENT_FINGER_DOWN:
{
    float x = event.tfinger.x * s->render_width;   // normalized 0-1 -> pixels
    float y = event.tfinger.y * s->render_height;
    // ... store event
}
```

## Hypothesis

The zigzag might be caused by:
1. **Touch event jitter** - iOS touch events have natural noise
2. **Frame rate mismatch** - Touch events arriving faster than frames rendered
3. **Coordinate precision** - Float precision issues in conversion

## Possible Solutions

### Option 1: Add Touch Smoothing
Implement a simple moving average or Bezier smoothing on input points before tessellation.

### Option 2: Use Mouse Events on Simulator
Desktop uses `SDL_EVENT_MOUSE_*` which work well. The iOS simulator might support mouse events.

### Option 3: Increase Interpolation
The current `+max-point-distance+` is 15px. We interpolate between points that are far apart, but maybe we need to SMOOTH points that are close together.

## Debug Info Needed

- Actual raw touch coordinates from iOS
- Whether the zigzag is in the INPUT or in the TESSELLATION
- Compare desktop mouse smoothness vs iOS touch

## Commands

```bash
# Rebuild with debug logs
touch DrawingMobile/drawing_mobile_ios.mm && make drawing-ios-jit-sim-run

# Check logs
xcrun simctl spawn booted log show --predicate 'process == "DrawingMobile-JIT-Sim"' --last 1m
```
