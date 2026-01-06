# Canvas Transform Fix & Quick-Pinch Reset

**Date**: 2025-01-05

## What I Learned

### 1. Coordinate Systems Matter!

The canvas transform had multiple issues because of mixing coordinate systems:

- **NDC (Normalized Device Coordinates)**: -1 to 1, Y=1 is top
- **Screen pixels**: 0 to width/height, Y=0 is top
- **Metal texture UV**: 0 to 1, Y=0 is top

**Key insight**: Do all transform math in ONE coordinate system (screen pixels), then convert at the end.

### 2. Shader and C++ Must Match Exactly

The canvas blit shader and `screenToCanvas()` C++ function must use **identical** inverse transform:

```cpp
// Both use this order:
// 1. Translate to pivot
// 2. Undo pan
// 3. Undo scale
// 4. Undo rotation
// 5. Translate back from pivot
```

### 3. Procreate Quick-Pinch Reset

Research from [Procreate Handbook](https://help.procreate.com/procreate/handbook/interface-gestures/gestures):

- **Quick pinch** (< 300ms) triggers canvas reset to default view
- Different from slow pinch (which zooms)
- Feels natural and discoverable

### 4. Smooth Animation with Easing

For the animated reset:
- **Ease-out cubic**: `1 - (1-t)³` - fast start, slow end
- **Angle interpolation**: Handle wrap-around (359° → 0° takes short path)
- **250ms duration**: Fast but visible

## Files Modified

1. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Fixed `screenToCanvas()` to match shader
   - Added `TwoFingerGesture.startTimeMs` and `startDistance` for quick-pinch detection
   - Added `CanvasResetAnimation` struct for smooth animated reset
   - Added `easeOutCubic()`, `lerp()`, `lerpAngle()` helper functions
   - Quick-pinch detection in `SDL_EVENT_FINGER_UP`
   - Animation update in render loop

2. **`src/vybe/app/drawing/native/stamp_shaders.metal`**
   - Simplified `canvas_blit_vertex` to work in screen pixel space
   - Removed confusing double Y-flips
   - Clear NDC → screen → inverse transform → UV pipeline

## Commands Run

```bash
# Build iOS simulator app
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj \
  -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' build

# Install and launch
xcrun simctl install 'iPad Pro 13-inch (M4)' \
  ~/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-*/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

## What's Next

1. **Undo-tree** - Emacs-style branchable undo history (researched, ready to implement)
2. **Animation recording** - Loom-style draw-and-record feature
3. **More brush types** - Expand beyond crayon

## Key Code Snippets

### Quick-Pinch Detection
```cpp
Uint64 gestureDuration = SDL_GetTicks() - gesture.startTimeMs;
float distanceRatio = endDistance / gesture.startDistance;

const Uint64 QUICK_GESTURE_MS = 300;
const float PINCH_THRESHOLD = 0.7f;

if (gestureDuration < QUICK_GESTURE_MS && distanceRatio < PINCH_THRESHOLD) {
    // Start animated reset
    resetAnim.isAnimating = true;
    resetAnim.startTimeMs = SDL_GetTicks();
    // ... store start/target values
}
```

### Ease-Out Cubic
```cpp
static float easeOutCubic(float t) {
    return 1.0f - powf(1.0f - t, 3.0f);
}
```

### Angle Interpolation (Shortest Path)
```cpp
static float lerpAngle(float a, float b, float t) {
    // Normalize to -PI to PI
    while (a > M_PI) a -= 2.0f * M_PI;
    while (a < -M_PI) a += 2.0f * M_PI;
    // ... same for b

    // Find shortest path
    float diff = b - a;
    if (diff > M_PI) diff -= 2.0f * M_PI;
    if (diff < -M_PI) diff += 2.0f * M_PI;

    return a + diff * t;
}
```
