# Apple Pencil Pressure & Eraser Mode Implementation

## What I Learned

### Key Technical Discoveries

1. **SDL3 Pen API on iOS**
   - Apple Pencil events are handled via `SDL_EVENT_PEN_DOWN`, `SDL_EVENT_PEN_MOTION`, `SDL_EVENT_PEN_UP`
   - **Pressure comes via separate `SDL_EVENT_PEN_AXIS` events** - NOT embedded in motion events
   - Axis types:
     - `SDL_PEN_AXIS_PRESSURE` (0) - Values 0.0 to 1.0
     - `SDL_PEN_AXIS_XTILT` (1) - Horizontal tilt in degrees
     - `SDL_PEN_AXIS_YTILT` (2) - Vertical tilt in degrees

2. **Pressure Tracking Pattern**
   ```cpp
   float pen_pressure = 1.0f;  // Track Apple Pencil pressure

   case SDL_EVENT_PEN_AXIS: {
       if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
           pen_pressure = event.paxis.value;
       }
       break;
   }

   case SDL_EVENT_PEN_MOTION: {
       if (is_drawing) {
           metal_stamp_undo_add_stroke_point(x, y, pen_pressure);
       }
       break;
   }
   ```

3. **Pressure Application in Brush Rendering**
   - `size_pressure` (0-1): How much pressure affects size
   - `opacity_pressure` (0-1): How much pressure affects opacity
   - Formula: `factor = 1.0 - pressure_setting + (pressure_setting * pressure)`
   - Default: `sizePressure=0.8, opacityPressure=0.3`

4. **Eraser Mode Implementation**
   - Simple approach: Paint with background color (white)
   - Toggle state with button tap
   - Restore color picker color when turning off eraser

### Eraser Button Pattern

```cpp
static bool g_eraserMode = false;

// Toggle eraser
g_eraserMode = !g_eraserMode;
if (g_eraserMode) {
    metal_stamp_set_brush_color(1.0f, 1.0f, 1.0f, 1.0f);  // White/bg
} else {
    metal_stamp_set_brush_color(colorPicker.currentR, colorPicker.currentG, colorPicker.currentB, 1.0f);
}
```

## Commands I Ran

```bash
# Build and run on simulator
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/drawing_build.txt

# Search for pressure-related code
grep -n "pen_pressure" DrawingMobile/drawing_mobile_ios.mm

# Check SDL3 pen structures
grep -A 15 "typedef struct SDL_PenMotionEvent" /opt/homebrew/include/SDL3/SDL_events.h
```

## Files Modified

1. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Added eraser mode state (`g_eraserMode`)
   - Added `EraserButtonConfig` struct and helper functions
   - Added eraser button drawing and touch handling (finger + pen)
   - Verified PEN_AXIS pressure handling (was already working)

2. **`src/vybe/app/drawing/native/metal_renderer.mm`**
   - Changed `setMaxNodes(50)` to `setMaxNodes(15)` for smaller undo memory footprint

## What's Next

1. **Test pressure visually** - Verify strokes vary in size/opacity with pencil pressure
2. **Consider tilt support** - Axis 1 & 2 provide tilt data for brush rotation
3. **Eraser brush mode** - Could use a dedicated eraser brush instead of just white color

## Procreate Brush Pressure Settings

Investigated actual Procreate brush files - each brush stores its own pressure curve:

| Brush | `dynamicsPressureSize` | `dynamicsPressureOpacity` | Behavior |
|-------|------------------------|---------------------------|----------|
| Ink brush | 0.6 | 0 | Size varies with pressure |
| Technical pen | 0 | 0 | Constant width (intentional) |
| Soft brush | 0.5 | 0 | Moderate pressure response |

**Key insight**: If a brush "doesn't respond to pressure", check if `dynamicsPressureSize = 0` in the brush file - that's **intentional** design, not a bug!

## Notes

- SDL3 iOS Apple Pencil support via UITouch - PR #11753 added this
- Pressure works on real device, not simulator
- Max undo nodes reduced from 50 to 15 (~210MB max memory)
- Fixed BrushImporter to set default pressure values BEFORE parsing (for brushes missing these keys)
