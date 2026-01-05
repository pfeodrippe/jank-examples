# Metal Retina Display & jank JIT Bug Fixes

**Date**: 2025-01-05

## What I Learned

### 1. jank JIT Limitation with Inline C++ Functions

**Critical Discovery**: Having TWO jank functions call the SAME inline C++ function causes a segfault.

For example, if both `rebuild-canvas-texture!` and `render-stroke-to-texture!` call `canvas/clear_canvas`, the JIT compilation succeeds but the app crashes before jank code runs.

**Workaround**: Consolidate all calls to the same inline C++ function into a single jank function. Don't duplicate inline C++ calls across multiple jank functions.

### 2. Retina Display Coordinate Systems

macOS Retina displays have two coordinate systems:
- **Points**: Window/UI coordinates (e.g., 1024x768)
- **Pixels/Drawable**: Actual render resolution (e.g., 2048x1536 at 2x scale)

Key insights:
- `CAMetalLayer.drawableSize` gives pixel dimensions
- SDL3 window size gives point dimensions
- Mouse events from SDL3 are in **points**
- Touch events (iOS) are already in render pixels
- Metal textures and rendering happen in **pixels**

### 3. Coordinate Scaling for Mouse Input

Mouse coordinates must be scaled from points to pixels:
```cpp
float scale_x = (float)render_width / (float)width;
float scale_y = (float)render_height / (float)height;
scaled_x = mouse_x * scale_x;
scaled_y = mouse_y * scale_y;
```

### 4. Metal Blit Operations

When blitting textures, use the actual texture dimensions, not assumed sizes:
```objc
NSUInteger srcWidth = texture.width;
NSUInteger srcHeight = texture.height;
```

## Files Modified

| File | Changes |
|------|---------|
| `src/vybe/app/drawing.jank` | Consolidated canvas operations to avoid JIT bug - removed duplicate inline C++ calls |
| `src/vybe/app/drawing/native/metal_renderer.mm` | Added `drawableWidth`/`drawableHeight` properties, fixed canvas texture creation to use drawable size, fixed blit to use actual texture dimensions |
| `src/vybe/app/drawing/native/drawing_canvas.hpp` | Added mouse coordinate scaling from points to render pixels |
| `DrawingMobile/drawing_mobile_ios.mm` | Added `metal_stamp_render_stroke()` call for real-time stroke rendering |
| `DrawingMobile/jank-resources/.../stamp_shaders.metal` | Fixed typo: `stamp_frtodoagment` → `stamp_fragment` |
| `src/vybe/app/drawing/native/stamp_shaders.metal` | Fixed typo: `stamp_frtodoagment` → `stamp_fragment` |

## Commands Run

```bash
# Run macOS drawing app
make drawing

# View output
./bin/run_drawing.sh
```

## Key Code Patterns

### Metal Renderer - Tracking Both Sizes
```objc
@property (nonatomic, assign) int width;           // Point size
@property (nonatomic, assign) int height;          // Point size
@property (nonatomic, assign) int drawableWidth;   // Pixel size
@property (nonatomic, assign) int drawableHeight;  // Pixel size
```

### Mouse Event Scaling
```cpp
case SDL_EVENT_MOUSE_MOTION:
    if ((event.motion.state & SDL_BUTTON_LMASK) && s->eventCount < MAX_EVENTS) {
        float scale_x = (float)s->render_width / (float)s->width;
        float scale_y = (float)s->render_height / (float)s->height;
        s->events[s->eventCount++] = {
            1,  // move
            event.motion.x * scale_x,
            event.motion.y * scale_y,
            1.0f,
            false
        };
    }
    break;
```

### 5. Metal Shader Function Name Typo

**Problem**: iOS Metal renderer failed with "Failed to find shader functions"

**Root Cause**: Typo in `stamp_shaders.metal` line 66:
```metal
fragment half4 stamp_frtodoagment(  // WRONG
fragment half4 stamp_fragment(      // CORRECT
```

The function name `stamp_frtodoagment` didn't match what the code was looking for (`stamp_fragment`).

**Fix**: Changed `stamp_frtodoagment` → `stamp_fragment` in both:
- `DrawingMobile/jank-resources/src/jank/vybe/app/drawing/native/stamp_shaders.metal`
- `src/vybe/app/drawing/native/stamp_shaders.metal`

### 6. iOS Real-Time vs End-of-Stroke Rendering

**Problem**: iOS only rendered strokes when touch ended, while macOS rendered in real-time.

**Root Cause**: The iOS `metal_test_main()` loop only called:
```cpp
metal_stamp_present();
```

But the macOS jank code calls BOTH:
```clojure
(canvas/metal_render_stroke)  ;; Renders in-progress stroke
(canvas/metal_present)
```

**Fix**: Added `metal_stamp_render_stroke()` call to iOS render loop:
```cpp
// Render the current stroke (for real-time preview) and present
metal_stamp_render_stroke();
metal_stamp_present();
```

**Rendering Flow**:
1. `add_stroke_point()` - Interpolates points and adds to buffer
2. `render_current_stroke()` → `renderPointsWithHardness:` - Renders buffer to canvas texture
3. `present()` - Blits canvas texture to screen

## What's Next

- All platforms working: macOS desktop, iOS Simulator, and iOS Device
- Both platforms now render strokes in real-time
- Consider investigating the jank JIT bug root cause for a proper fix in the jank compiler
- Add more brush parameters (size, hardness, opacity controls)
