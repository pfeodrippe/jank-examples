# iOS Touch Zigzag Fix - Final Solution

**Date**: 2025-01-05
**Problem**: Drawing on iOS produces zigzag lines and is slow
**Status**: FIXED (verified with MCP simulator testing)

## Root Cause Analysis

The zigzag issue had TWO causes:

### 1. Incorrect Coordinate Conversion
SDL3 touch events (`SDL_TouchFingerEvent`) provide **normalized coordinates (0.0-1.0)** relative to the window.
Simply multiplying by window size or render size was WRONG because it doesn't account for:
- High DPI scaling
- Viewport settings
- Logical presentation mode
- Render scale

**Solution**: Use `SDL_ConvertEventToRenderCoordinates(renderer, &event)` which:
- Converts normalized touch coords to render coordinates automatically
- Handles ALL DPI and scaling factors
- Modifies the event in-place

### 2. Multi-Touch Mixing (CRITICAL!)
Without fingerID filtering, multiple touch sources could get mixed together:
- Different finger IDs from multi-touch
- Phantom touches
- Touch events from different sources

**Solution**: Track active fingerID and only process events from ONE finger at a time.

## Implementation

### Key Changes in `drawing_canvas.hpp`:

```cpp
// Add to CanvasState struct:
SDL_FingerID activeFingerID = 0;
bool touchActive = false;

// Touch event handling:
case SDL_EVENT_FINGER_DOWN:
    if (!s->touchActive) {
        s->touchActive = true;
        s->activeFingerID = event.tfinger.fingerID;
        SDL_ConvertEventToRenderCoordinates(s->renderer, &event);
        // Use event.tfinger.x/y directly (already converted!)
        s->events[s->eventCount++] = {0, event.tfinger.x, event.tfinger.y, ...};
    }
    break;

case SDL_EVENT_FINGER_MOTION:
    if (s->touchActive && event.tfinger.fingerID == s->activeFingerID) {
        SDL_ConvertEventToRenderCoordinates(s->renderer, &event);
        s->events[s->eventCount++] = {1, event.tfinger.x, event.tfinger.y, ...};
    }
    break;

case SDL_EVENT_FINGER_UP:
    if (s->touchActive && event.tfinger.fingerID == s->activeFingerID) {
        SDL_ConvertEventToRenderCoordinates(s->renderer, &event);
        s->events[s->eventCount++] = {2, event.tfinger.x, event.tfinger.y, ...};
        s->touchActive = false;
        s->activeFingerID = 0;
    }
    break;
```

### iOS Logging (for debugging):

```cpp
#ifdef __APPLE__
#include <os/log.h>
#define TOUCH_LOG(fmt, ...) os_log_info(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__)
#else
#define TOUCH_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif
```

## What DOESN'T Work

1. **Manual multiplication by window/render size** - Doesn't handle DPI scaling correctly
2. **Not filtering by fingerID** - Multi-touch causes zigzag as different touches get mixed

## Important Notes

1. **C++ headers in iOS JIT apps are AOT compiled** - Changes to `.hpp` files require rebuilding the Xcode project, not just restarting the compile server
2. **Touch the .mm file to force rebuild** - Xcode may not detect header changes: `touch DrawingMobile/drawing_mobile_ios.mm`
3. **Never edit jank-resources directly** - These files are synced from `src/` during build

## Testing

Verified smooth lines using iOS Simulator MCP:
- Diagonal lines: SMOOTH
- Vertical lines: SMOOTH
- Multiple directions: ALL SMOOTH

## Sources

- [SDL_ConvertEventToRenderCoordinates](https://wiki.libsdl.org/SDL3/SDL_ConvertEventToRenderCoordinates)
- [SDL_TouchFingerEvent](https://wiki.libsdl.org/SDL3/SDL_TouchFingerEvent)
- [GitHub Issue #4159 - Touch coordinate normalization](https://github.com/libsdl-org/SDL/issues/4159)

## Commands Used

```bash
# Rebuild iOS app (forces recompilation of headers)
touch DrawingMobile/drawing_mobile_ios.mm && make drawing-ios-jit-sim-run

# Check simulator logs for touch events
xcrun simctl spawn booted log stream --level info --predicate 'process == "DrawingMobile-JIT-Sim"'

# Test drawing with MCP
# (use mcp__ios-simulator__ui_swipe)
```
