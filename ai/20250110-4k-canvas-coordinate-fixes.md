# 4K Canvas Coordinate System Fixes

## Problem
When implementing a 4K canvas (3840x2160) independent of screen size, several coordinate-related issues appeared:

1. **Double transformation bug**: Coordinates were being transformed twice - once in `drawing_mobile_ios.mm` and again in `metal_renderer.mm`
2. **Canvas not centered**: The 4K canvas wasn't centered on screen when aspect ratios differ
3. **Drawing lost after transform**: Strokes disappeared or were partially visible when pan/zoom/rotate was applied
4. **Wrong viewport for stroke rendering**: Strokes were rendered to screen-sized viewport, not 4K canvas

## Root Causes

### 1. Double Transformation
- `screenToCanvas()` in `drawing_mobile_ios.mm` converts screen coords → canvas coords
- `screenToNDC()` in `metal_renderer.mm` was ALSO applying the transform
- **Fix**: `screenToNDC()` should only convert canvas pixels → NDC, no transform needed

### 2. Wrong Pan Formula for Centering
- Incorrect: `pan = screenCenter - canvasCenter * scale`
- Correct: `pan = (screenCenter - canvasCenter) * scale`
- These give very different results when canvas is larger than screen

### 3. Screen Size vs Canvas Size References
Multiple places incorrectly used `drawableWidth/Height` or `self.width/height` instead of `canvasWidth/Height`:
- `interpolateFrom:to:` - distance calculation for stroke interpolation
- `setViewport` - missing entirely, defaulted to drawable size
- Delta snapshot functions
- Frame cache blit operations

## Files Modified

### `/Users/pfeodrippe/dev/something/src/vybe/app/drawing/native/metal_renderer.mm`
- Simplified `screenToNDC()` to just convert canvas pixels → NDC (no transform)
- Added `setViewport` with canvas dimensions in `renderPointsWithHardness`
- Fixed `interpolateFrom:to:` to use `canvasWidth/Height` for distance calculation
- Fixed scatter calculation to use `canvasWidth`
- Fixed delta snapshot functions to use canvas size
- Fixed frame cache blit operations to use canvas size

### `/Users/pfeodrippe/dev/something/DrawingMobile/drawing_mobile_ios.mm`
- Added default transform calculation with `defaultScale` and centered `defaultPan`
- Fixed pan formula: `(screenCenter - canvasCenter) * scale`
- Applied default values to `canvasTransform` and `resetAnim`

### `/Users/pfeodrippe/dev/something/Makefile`
- Added `pkill -f "DrawingMobile-JIT-Sim"` as fallback for app termination
- Added `sleep 1` after termination to ensure clean restart

## Key Learnings

1. **Single source of truth**: Canvas size should be defined in ONE place. Currently it's hardcoded in both `metal_renderer.mm` (3840x2160) and `drawing_mobile_ios.mm` - this should be refactored.

2. **Coordinate space clarity**: Always be clear about which coordinate space you're in:
   - Screen points (touch input)
   - Drawable pixels (screen rendering)
   - Canvas pixels (4K texture)
   - NDC (-1 to 1, for shaders)

3. **Transform flow**:
   - Input: `screenToCanvas()` converts touch → canvas coords (applies inverse view transform)
   - Stroke: `screenToNDC()` converts canvas coords → NDC (just a division)
   - Display: Shader applies forward view transform to display canvas

4. **Viewport for off-screen rendering**: When rendering to a texture larger than screen, MUST explicitly set viewport to texture size.

## Commands Used
```bash
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/build_output.txt
grep -n "drawableWidth\|canvasWidth" metal_renderer.mm
```

## Next Steps
- Refactor canvas size to be configurable from one place
- Consider making canvas resolution a user setting
