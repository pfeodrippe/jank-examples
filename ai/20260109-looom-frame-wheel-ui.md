# Looom-Style Frame Wheel UI Implementation

## What I Learned

### Frame Wheel Design
- **Circular ring UI** positioned at bottom-right of screen for frame navigation
- **Ring segments** drawn using many small rectangles approximating a circle
- **Frame indicators** shown as dots around the ring, with current frame highlighted
- **Rotation-based navigation** - drag around the wheel to change frames

### Key Implementation Details
1. **FrameWheel struct** stores position, radii (outer/inner for donut shape), and drag state
2. **metal_stamp_queue_ui_rect** requires corner_radius parameter (9th param)
3. **Touch handling** checks if point is within ring annulus (between inner and outer radius)
4. **Angle-to-frame mapping** converts rotation angle delta to frame number changes

### Integration Points
- `anim_get_frame_count()` - get total frames
- `anim_get_current_frame_index()` - get current frame
- `anim_goto_frame(index)` - jump to specific frame
- `anim_render_current_frame()` - re-render after frame change
- `metal_stamp_clear_canvas()` - clear before re-render

## Commands I Ran

```bash
# Build iOS app
make drawing-ios-jit-sim-build

# Run in simulator
make drawing-ios-jit-sim-run
```

## Files Modified

### DrawingMobile/drawing_mobile_ios.mm
- Added `FrameWheel` struct (line ~1197)
- Added `drawFrameWheel()` function to render the wheel
- Added `isPointInFrameWheel()` and `getWheelAngle()` helpers
- Initialized frameWheel variable in main loop
- Added touch handling in FINGER_DOWN, FINGER_MOTION, FINGER_UP events
- Added drawFrameWheel() call in render section before metal_stamp_present()

## Key Code Snippets

### FrameWheel struct
```cpp
struct FrameWheel {
    float centerX, centerY;  // Center position
    float radius;            // Outer radius
    float innerRadius;       // Inner radius (donut shape)
    bool isDragging;
    float dragStartAngle;
    int dragStartFrame;
};
```

### Wheel position calculation
```cpp
FrameWheel frameWheel = {
    .centerX = width - 80.0f,     // Bottom-right corner
    .centerY = height - 120.0f,
    .radius = 50.0f,
    .innerRadius = 25.0f,
    ...
};
```

### Angle-to-frame conversion
```cpp
float angleDelta = currentAngle - frameWheel.dragStartAngle;
// Normalize to -PI to PI
while (angleDelta > PI) angleDelta -= 2 * PI;
while (angleDelta < -PI) angleDelta += 2 * PI;
// Map angle to frames
int frameDelta = (int)(angleDelta / (2 * PI) * totalFrames);
int newFrame = frameWheel.dragStartFrame + frameDelta;
```

## What's Next

1. Test frame wheel interaction in simulator
2. Fine-tune wheel size and position for better UX
3. Add visual feedback when dragging (highlight wheel)
4. Consider adding frame count display in center of wheel
5. Add tap-to-add-frame functionality (tap center to add new frame)
