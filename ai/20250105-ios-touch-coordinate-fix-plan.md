# iOS Touch Coordinate Fix Plan

## Problem Analysis

**Desktop works fine, iOS has zigzag lines.**

### Root Cause Investigation

Looking at `drawing_canvas.hpp`:

**Desktop (MOUSE events) - Lines 224-258:**
```cpp
case SDL_EVENT_MOUSE_BUTTON_DOWN:
    s->events[s->eventCount++] = {
        0,
        event.button.x,      // <-- ALREADY IN PIXEL COORDINATES!
        event.button.y,
        1.0f,
        false
    };
```
Mouse events give **absolute pixel coordinates** - no conversion needed.

**iOS (FINGER/TOUCH events) - Lines 261-295:**
```cpp
case SDL_EVENT_FINGER_DOWN:
    s->events[s->eventCount++] = {
        0,
        event.tfinger.x * s->width,   // <-- NORMALIZED (0.0-1.0) * WINDOW SIZE
        event.tfinger.y * s->height,
        event.tfinger.pressure,
        false
    };
```
Touch events are **NORMALIZED (0.0-1.0)** and multiplied by `width`/`height`.

### The High-DPI Problem

On Retina/high-DPI displays:
- **Window size** (logical pixels): e.g., 1024x768
- **Render output size** (physical pixels): e.g., 2048x1536 (2x scale factor)

SDL touch coordinates are normalized 0.0-1.0 and need to be scaled to **render output size**, NOT window size!

Current behavior:
1. Touch at center of screen: `tfinger.x = 0.5, tfinger.y = 0.5`
2. Multiply by window size: `x = 0.5 * 1024 = 512, y = 0.5 * 768 = 384`
3. But rendering happens at 2048x1536 scale!
4. The coordinates 512,384 end up at 1/4 of the screen position
5. Result: zigzag as points are mapped incorrectly

### Solution

Use `SDL_GetRenderOutputSize()` to get actual render dimensions and use those for touch coordinate scaling.

## Implementation Steps

### Step 1: Add render dimensions to CanvasState struct
```cpp
struct CanvasState {
    // SDL
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int width = 1024;           // Window size (logical pixels)
    int height = 768;
    int render_width = 1024;    // Render output size (physical pixels)
    int render_height = 768;
    // ... rest
};
```

### Step 2: Get render output size after creating renderer in init()
```cpp
// After SDL_CreateRenderer:
SDL_GetRenderOutputSize(s->renderer, &s->render_width, &s->render_height);
std::cout << "[drawing_canvas] Render output size: "
          << s->render_width << "x" << s->render_height << std::endl;
```

### Step 3: Update touch event handlers to use render dimensions
```cpp
case SDL_EVENT_FINGER_DOWN:
    s->events[s->eventCount++] = {
        0,
        event.tfinger.x * s->render_width,   // Use render_width
        event.tfinger.y * s->render_height,  // Use render_height
        event.tfinger.pressure,
        false
    };
// Same for FINGER_MOTION and FINGER_UP
```

### Step 4: Update canvas texture creation to use render size
```cpp
s->canvas_texture = SDL_CreateTexture(s->renderer,
    SDL_PIXELFORMAT_RGBA8888,
    SDL_TEXTUREACCESS_TARGET,
    s->render_width, s->render_height);  // Use render dimensions
```

### Step 5: Update get_width/get_height to return render dimensions
```cpp
inline int get_width() {
    auto* s = get_state();
    return s ? s->render_width : 0;
}

inline int get_height() {
    auto* s = get_state();
    return s ? s->render_height : 0;
}
```

### Step 6: Handle window resize events
```cpp
case SDL_EVENT_WINDOW_RESIZED:
    s->width = event.window.data1;
    s->height = event.window.data2;
    // Also update render size
    SDL_GetRenderOutputSize(s->renderer, &s->render_width, &s->render_height);
    break;
```

## Testing

1. Rebuild: `make drawing-ios-jit-sim-run` (handles everything: sync, compile server restart, app terminate & relaunch)
2. Draw in simulator - should see smooth lines
3. Verified via iOS Simulator MCP - lines are now SMOOTH!

## Files to Modify

- **ORIGINAL**: `src/vybe/app/drawing/native/drawing_canvas.hpp`
- **NOT**: `DrawingMobile/jank-resources/...` (this is derived/synced, never edit directly!)

## Key Lessons Learned

1. **NEVER edit files in jank-resources** - these are synced from `src/` during build
2. **Use `lsof -ti:PORT | xargs kill`** instead of `pkill -f` to kill only specific port holders
3. **Makefile should terminate app before relaunch** to pick up new JIT-compiled code
4. **SDL touch coordinates (tfinger.x/y) are normalized 0.0-1.0** - must multiply by RENDER size, not window size
5. **SDL_GetRenderOutputSize()** gives actual physical pixels (for high-DPI), different from window size
