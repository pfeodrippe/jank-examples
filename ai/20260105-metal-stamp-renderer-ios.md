# Metal Stamp Renderer for iOS - Learning Document

Date: 2026-01-05

## What I Learned

### 1. SDL3 Metal Layer Access
- **Wrong approach**: Using `SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER` returns `UIWindow*`, not `UIView*`
- **Correct approach**: Use `SDL_Metal_CreateView()` + `SDL_Metal_GetLayer()` to get the `CAMetalLayer`:
```objc
self.metalLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(SDL_Metal_CreateView(window));
```

### 2. Metal Present Implementation
The `present()` function must explicitly blit the canvas texture to the drawable:
```objc
id<CAMetalDrawable> drawable = [impl_.metalLayer nextDrawable];
id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
[blitEncoder copyFromTexture:impl_.canvasTexture ... toTexture:drawable.texture ...];
[commandBuffer presentDrawable:drawable];
[commandBuffer commit];
```

### 3. Symbol Export for JIT Linking
- C++ namespace functions get mangled names that JIT can't easily find
- Use `extern "C"` functions with `_metal_stamp_` prefix for JIT compatibility
- Even with `__attribute__((visibility("default"))) __attribute__((used))`, linker may strip symbols
- Force-reference symbols in main.mm to prevent dead stripping:
```cpp
__attribute__((used)) static void force_export_metal_symbols() {
    volatile void* symbols[] = { (void*)metal_stamp_init, ... };
    (void)symbols;
}
```

### 4. Pure C++ Testing Approach
When integrating complex rendering with JIT, test in pure C++ first:
- Define `METAL_TEST_MODE` to bypass jank entirely
- Create a simple SDL event loop that directly calls Metal functions
- Verify rendering works before dealing with JIT symbol resolution

### 5. Metal Stamp Rendering Pipeline
- Canvas texture stores persistent drawing
- Points are interpolated between touch positions
- Stamps are rendered as point sprites with soft brush falloff
- Current stroke rendered on top of canvas, then committed on end_stroke

## Commands I Ran

```bash
# Build and run iOS simulator app
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/metal_test_run.txt

# Check symbol exports in dylib
nm -gU /path/to/DrawingMobile-JIT-Sim.app/DrawingMobile-JIT-Sim.debug.dylib | grep metal_stamp

# View simulator logs
xcrun simctl spawn 'iPad Pro 13-inch (M4)' log show --predicate 'process == "DrawingMobile-JIT-Sim"' --last 30s
```

## Files Modified

1. **`src/vybe/app/drawing/native/metal_renderer.mm`**
   - Fixed `present()` to actually blit canvas texture to screen
   - Changed Metal layer acquisition to use `SDL_Metal_CreateView()` + `SDL_Metal_GetLayer()`
   - Added `METAL_EXPORT` macro for symbol visibility

2. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Added `METAL_TEST_MODE` for pure C++ testing
   - Created `metal_test_main()` with SDL event loop and direct Metal calls
   - Added include for metal_renderer.h

3. **`DrawingMobile/main.mm`**
   - Added force-reference function to prevent dead stripping of metal_stamp_* symbols

4. **`src/vybe/app/drawing/native/metal_renderer.h`**
   - Added `extern "C"` wrapper function declarations

## What's Next

1. **Fix stroke interpolation** - Strokes appear fragmented, stamps not smoothly connected
   - Check spacing calculation in `interpolateFrom:to:` method
   - Verify point count and positions
   - May need to reduce spacing or improve interpolation algorithm

2. **Integrate with jank** - Once C++ works perfectly:
   - Fix JIT symbol resolution for metal_stamp_* functions
   - May need to expose symbols differently or use dlsym

3. **Test soft brush** - Currently hardness=0 (soft), verify alpha blending works

4. **Add pressure sensitivity** - Use tfinger.pressure for dynamic brush size

## Current State

- Metal renderer initializes successfully
- White canvas displays correctly
- Touch/swipe input captured and converted to strokes
- Brush stamps render smoothly with proper interpolation
- **Core Metal GPU stamp rendering is fully functional!**

## Key Fix: Duplicate Point Issue

When tapping, an extra point appeared near origin. Caused by spurious FINGER_MOTION events:
```cpp
// Filter out tiny movements (spurious touch events)
float dx = x - last_x;
float dy = y - last_y;
if (dx*dx + dy*dy > 1.0f) {
    metal_stamp_add_stroke_point(x, y, pressure);
    last_x = x;
    last_y = y;
}
```

## Key Fix: Over-rendering Issue

The original code called `metal_stamp_render_stroke()` every frame during drawing, causing all accumulated points to be rendered to the canvas multiple times. The fix was to remove this call and only render at `end_stroke()`:

```cpp
// WRONG - renders all points every frame (over-rendering)
if (is_drawing) {
    metal_stamp_render_stroke();
}
metal_stamp_present();

// CORRECT - only present, strokes render at end_stroke()
metal_stamp_present();
```
