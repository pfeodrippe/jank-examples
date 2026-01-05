# iOS Touch Zigzag Investigation - Detailed Plan

**Date**: 2025-01-05
**Status**: IN PROGRESS
**Goal**: Draw 2 smooth triangles side by side to verify fix

## The Puzzle

We have a CRITICAL observation that reveals the root cause:

| Source | Line Quality | Persistence |
|--------|--------------|-------------|
| MCP swipes (idb) | SMOOTH | NO - erases previous |
| Manual touches | ZIGZAG | YES - strokes accumulate |

**This is in the SAME running app!** This is the key insight.

## Hypothesis Analysis

### Theory 1: Different Coordinate Systems
MCP's `idb` injects touches at the iOS/UIKit level. These might be:
- Already in the correct coordinate system (screen pixels)
- Processed differently by SDL's iOS backend

Manual touches go through:
1. UIKit touch handling
2. SDL's iOS backend translation
3. SDL_TouchFingerEvent generation

**Test**: Log both MCP and manual touch coordinates with same format.

### Theory 2: Persistence Issue Reveals State Problem
The fact that MCP strokes DON'T persist but ARE smooth suggests:
- MCP might be using a DIFFERENT fingerID for each event
- FingerID filtering (if present) might be rejecting some events
- Or canvas texture caching isn't working for MCP strokes

**Check**: Is there fingerID tracking that's filtering MCP events?

### Theory 3: SDL_TouchFingerEvent Coordinate Format
According to SDL3 docs:
```
SDL_TouchFingerEvent {
    float x;  // normalized 0..1, relative to window
    float y;  // normalized 0..1, relative to window
}
```

But GitHub issue #12491 suggests iOS might give POINTS directly...

**Current code multiplies by display scale (2.0):**
- If coords are 0-1 normalized: scale*coord = 0-2 pixels (WRONG!)
- If coords are POINTS (0-512): scale*coord = 0-1024 pixels (might be correct)

## The Real Issue

Looking at the current code:
```cpp
float scale = SDL_GetWindowDisplayScale(s->window);
float x = event.tfinger.x * scale;
float y = event.tfinger.y * scale;
```

If touch coords are NORMALIZED (0-1), we should be doing:
```cpp
float x = event.tfinger.x * render_width;  // Multiply by canvas size
float y = event.tfinger.y * render_height;
```

If touch coords are in POINTS, we should be doing:
```cpp
float scale = SDL_GetWindowDisplayScale(s->window);
float x = event.tfinger.x * scale;  // Points to pixels
float y = event.tfinger.y * scale;
```

## Investigation Steps

### Step 1: Add Comprehensive Logging
Log ALL coordinate details:
- Raw event coords
- Window size (SDL_GetWindowSize)
- Render size (SDL_GetRenderOutputSize)
- Display scale (SDL_GetWindowDisplayScale)
- Result after conversion

### Step 2: Test Both Coordinate Theories
Create TWO conversion modes:
1. Normalized mode: `x * render_width`
2. Points mode: `x * display_scale`

Log which produces coordinates in the expected 0-2048 range.

### Step 3: Fix the Conversion
Apply the correct formula based on logging.

### Step 4: Verify with 2 Triangles
Draw:
- Triangle 1: Left side of screen
- Triangle 2: Right side of screen
- Each triangle = 3 lines

## Expected Coordinate Ranges

For our 1024x1024 canvas rendered at 2x (2048x2048 render target):
- **If normalized (0-1)**: Raw coords 0-1, need to multiply by 2048
- **If points**: Raw coords 0-1024, need to multiply by 2 (display scale)
- **If already pixels**: Raw coords 0-2048, no conversion needed

## Code Changes Needed

```cpp
case SDL_EVENT_FINGER_DOWN:
{
    // Get ALL sizes for debugging
    int win_w, win_h;
    SDL_GetWindowSize(s->window, &win_w, &win_h);

    int render_w, render_h;
    SDL_GetRenderOutputSize(s->renderer, &render_w, &render_h);

    float scale = SDL_GetWindowDisplayScale(s->window);

    TOUCH_LOG("[TOUCH] raw=(%.3f,%.3f) win=(%d,%d) render=(%d,%d) scale=%.2f",
              event.tfinger.x, event.tfinger.y,
              win_w, win_h, render_w, render_h, scale);

    // Try SDL's built-in conversion first
    SDL_Event evt_copy = event;
    SDL_ConvertEventToRenderCoordinates(s->renderer, &evt_copy);

    TOUCH_LOG("[TOUCH] after SDL convert: (%.1f,%.1f)",
              evt_copy.tfinger.x, evt_copy.tfinger.y);

    // Use SDL's converted coordinates
    float x = evt_copy.tfinger.x;
    float y = evt_copy.tfinger.y;

    // ... store event
}
```

## Next Actions

1. Rebuild with enhanced logging
2. Test manual touch - observe raw coordinates
3. Test MCP swipe - compare coordinates
4. Fix conversion formula based on findings
5. Draw 2 triangles to verify

## Commands

```bash
# Rebuild (touch .mm to force header recompile)
touch /Users/pfeodrippe/dev/something/DrawingMobile/drawing_mobile_ios.mm
make drawing-ios-jit-sim-run

# Watch simulator logs
xcrun simctl spawn booted log stream --level info --predicate 'process == "DrawingMobile-JIT-Sim"' 2>&1 | grep TOUCH
```
