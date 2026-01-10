# Pulley Time Navigation (Looom-style)

## Goal
Implement Looom's "Pulley" system for frame navigation using one finger anywhere on the canvas.

## How It Works
1. **Quick swipe up/down** (before 150ms) → Go to prev/next frame immediately, no pulley shown
2. **Hold finger 150ms+** → Pulley appears to the LEFT of finger (3x size, radius=180px)
3. **Rotate finger around pulley center** → Scrub through frames in real-time
4. **Finger at 3 o'clock** = startFrame (no change)
5. **Works alongside pencil drawing** - palm rejection removed for pulley control

## Implementation Details

### Pulley Struct
```cpp
struct Pulley {
    bool active;              // Is pulley currently shown
    bool pending;             // Waiting for delay before showing
    Uint64 touchStartMs;      // When finger first touched
    float startX, startY;     // Initial finger position
    float centerX, centerY;   // Pulley center (left of finger touch)
    float fingerX, fingerY;   // Current finger position (for drawing indicator)
    int startFrame;           // Frame when touch began
    float radius;             // Visual radius (180px)
};
```

### Touch Event Flow
1. **FINGER_DOWN on canvas**:
   - Set `pending = true`, record `touchStartMs`, `startX/Y`, `startFrame`
   - Don't save frame yet (wait for activation)

2. **FINGER_MOTION while pending**:
   - If quick swipe (< 150ms, > 60px vertical) → frame_next/prev, cancel pending
   - If delay passed (>= 150ms) → activate pulley, save frame, set center

3. **FINGER_MOTION while active**:
   - Calculate angle from center to finger
   - Map angle to frame offset (12 frames = 2π)
   - Load frame if changed

4. **FINGER_UP**:
   - Deactivate pulley and cancel pending

### Key Constants
- `PULLEY_DELAY_MS = 150` - Time before pulley appears
- `SWIPE_THRESHOLD = 60.0f` - Vertical distance for quick swipe
- `radius = 180.0f` - Pulley visual size (3x original)

## Files Modified
- `DrawingMobile/drawing_mobile_ios.mm`:
  - Added Pulley struct with pending/active states
  - Added drawPulley function (uses metal_stamp_queue_ui_rect)
  - Modified FINGER_DOWN/MOTION/UP for pending → active flow
  - Removed palm rejection for finger events (allows pulley + pencil)
  - Added drawPulley() call in render loop

## Commands
```bash
make drawing-ios-jit-sim-run
```

## What's Next
- Test on device
- Consider adding visual feedback during pending state
- Maybe add haptic feedback on frame change
