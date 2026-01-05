# iOS High-DPI Coordinate Fix for DrawingMobile

**Date**: 2025-01-05

## Problem

Drawing in the iOS simulator produced zigzag/jagged lines instead of smooth strokes. The issue was a **high-DPI coordinate mismatch** between touch event coordinates and the rendering surface.

## Root Cause

On high-DPI (Retina) displays, there's a difference between:
- **Window size** (logical pixels) - e.g., 1024x768
- **Render size** (physical pixels) - e.g., 2048x1536 (2x scale)

SDL3 touch events (`SDL_EVENT_FINGER_DOWN/MOVE/UP`) provide normalized coordinates (0.0-1.0) via `event.tfinger.x/y`. These need to be scaled to the **render size**, not the window size.

The original code was doing:
```cpp
x: event.tfinger.x * s->width,    // WRONG - uses window size
y: event.tfinger.y * s->height,
```

This caused coordinates to be off by a factor of 2 on Retina displays, resulting in zigzag patterns.

## Solution

Added separate tracking for render dimensions and used them for touch coordinate mapping:

```cpp
// In CanvasState struct:
int width = 1024;          // Window size (logical pixels)
int height = 768;
int render_width = 1024;   // Render size (physical pixels for high-DPI)
int render_height = 768;

// In init():
SDL_GetRenderOutputSize(s->renderer, &s->render_width, &s->render_height);

// Touch event handling - use render dimensions:
case SDL_EVENT_FINGER_DOWN:
    s->events[s->eventCount++] = {
        0,  // EVENT_DOWN
        event.tfinger.x * s->render_width,   // CORRECT - uses render size
        event.tfinger.y * s->render_height,
        event.tfinger.pressure,
        false
    };
```

## Key Files Modified

1. **`DrawingMobile/jank-resources/src/jank/vybe/app/drawing/native/drawing_canvas.hpp`**
   - Added `render_width` and `render_height` fields to `CanvasState`
   - Call `SDL_GetRenderOutputSize()` in `init()`
   - Updated `get_width()`/`get_height()` to return render dimensions
   - Fixed all touch event handlers to use render dimensions

2. **`src/vybe/app/drawing.jank`**
   - Added optional `nrepl-port` parameter to `run-app!` and `-main`
   - Default port 5580 for desktop, 5581 for iOS

3. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Pass port 5581 to `-main` function

## Commands Used

```bash
# Rebuild and run iOS JIT simulator
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/drawing_build.txt

# Terminate and relaunch app to pick up new JIT code
xcrun simctl terminate 57653CE6-DF09-4724-8B28-7CB6BA90E0E3 com.vybe.DrawingMobile
xcrun simctl launch 57653CE6-DF09-4724-8B28-7CB6BA90E0E3 com.vybe.DrawingMobile
```

## Testing

Used iOS Simulator MCP tools to verify:
- `mcp__ios-simulator__ui_swipe` - Draw lines programmatically
- `mcp__ios-simulator__screenshot` - Capture results

**Result**: Lines are now smooth, no zigzag artifacts.

## Key Learnings

1. **Always use `SDL_GetRenderOutputSize()`** for high-DPI displays, not `SDL_GetWindowSize()`
2. **SDL touch coordinates are normalized (0.0-1.0)** - must multiply by actual render dimensions
3. **JIT code changes require app restart** - terminate and relaunch to pick up new compiled code
4. **nREPL ports**: Use 5580 for desktop, 5581 for iOS to avoid conflicts when both run

## Next Steps

- Consider adding zoom/pan support to the canvas
- Add pressure sensitivity visualization (line width varies with pressure)
- Test on actual iPad device
