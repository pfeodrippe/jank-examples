# Scrollable Brush Picker Implementation

## What I Learned

### Key Technical Discoveries

1. **Scroll State Management for UI Panels**
   - Used global static variables to track scroll state across touch events:
     - `g_brushPickerScrollOffset` - current scroll position in pixels
     - `g_brushPickerIsDragging` - whether user is dragging to scroll
     - `g_brushPickerDragStartY` - where touch started
     - `g_brushPickerDragStartOffset` - scroll offset when drag started
     - `g_brushPickerDragFingerId` - which finger is scrolling

2. **Touch Event Flow for Scroll vs Tap Detection**
   - FINGER_DOWN: Start tracking potential scroll (store start position)
   - FINGER_MOTION: Update scroll offset based on drag delta
   - FINGER_UP: Check total movement - if < 15px treat as tap (select brush), otherwise it was a scroll

3. **Hit Testing with Scroll Offset**
   - When checking which brush item was tapped, add scroll offset to get "virtual" position
   - `relY = py - gridY + g_brushPickerScrollOffset` converts screen Y to content Y

4. **Drawing with Scroll Offset**
   - Apply negative offset to item positions: `itemY = gridY + row * cellSize - g_brushPickerScrollOffset`
   - Cull items outside visible area (both above and below)
   - Draw scroll indicator showing position and size

5. **Scroll Indicator Calculation**
   - `scrollRatio = visibleHeight / contentHeight` - proportion of content visible
   - `scrollBarHeight = max(40px, visibleHeight * scrollRatio)` - minimum 40px thumb
   - `scrollProgress = scrollOffset / maxScroll` - position as 0-1 ratio

### Patterns Used

- **Offset Clamping**: Clamp scroll offset in draw function to ensure valid range
- **Dual Input Support**: Both finger scrolling and Apple Pencil taps work with shared scroll offset
- **Visibility Culling**: Skip items completely outside visible area for performance

## Commands I Ran

```bash
# Sync source files and build
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/drawing_build.txt

# Check brushsets exist
ls -la /Users/pfeodrippe/dev/something/DrawingMobile/*.brushset

# Check for build errors
cat /tmp/drawing_build.txt | grep -A 20 "error:"
```

## Build Fix

Found leftover reference to `g_pre_stroke_pixels` at lines 2373-2374 from the delta undo reversion. Removed the lines:

```cpp
// Removed these lines from undo_cancel_stroke():
g_pre_stroke_pixels.clear();
g_pre_stroke_pixels.shrink_to_fit();
```

## Files Modified

1. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Added scroll state static variables (lines 285-290)
   - Updated `BrushPickerConfig` struct to use float scrollOffset
   - Updated `getBrushAtPoint()` to account for scroll offset in hit testing
   - Updated `drawBrushPicker()` to:
     - Calculate content height and max scroll
     - Apply scroll offset to item positions
     - Cull items outside visible area
     - Draw scroll indicator bar
   - Modified FINGER_DOWN handler to track scroll start
   - Added FINGER_MOTION handler for scroll updates
   - Modified FINGER_UP handler to detect tap vs scroll

## What's Next

1. **Regenerate Xcode Project** - Run `xcodegen` in DrawingMobile folder to include BetterThanBasics.brushset
2. **Test Scrolling** - Verify finger scrolling works correctly
3. **Test Brush Selection** - Ensure taps still select brushes after scrolling
4. **Test Both Brushsets** - Confirm all brushes from both sets appear and are selectable

## Notes

- The undo system was reverted to simple full snapshots (no delta undo)
- Using `snapshotInterval = 1` means every stroke gets a full snapshot
- Max 50 undo nodes to limit memory usage (~700MB max for 50 x 14MB snapshots)
