# Brush Thumbnail Investigation - January 6, 2026

## Problem Summary
Brush thumbnail previews are not visible in the iOS simulator. User wants:
1. Fix the brush thumbnail display
2. Add a more intentional brush picker button (pencil icon)

## Investigation Findings

### 1. Brush Button Position Issue (CRITICAL)
From simulator logs:
```
[DEBUG] Tap at (260.0, 1038.0) - Brush button at (2582.0, 1814.0) size (200.0x60.0)
```

The brush button is positioned at `(2582, 1814)` but the coordinate system is rotated 90 degrees for landscape mode. This positioning makes the button hard to find or off-screen in certain orientations.

### 2. Coordinate System Analysis
From `drawing_mobile_ios.mm:1263-1296`:
- `textureButtonsX = width - TEXTURE_BUTTON_SIZE * 2 - TEXTURE_BUTTON_GAP - 40.0f`
- `textureButtonsY = height - TEXTURE_BUTTON_SIZE * 2 - TEXTURE_BUTTON_GAP - 40.0f`
- Brush button: `x = textureButtonsX`, `y = textureButtonsY - BRUSH_BUTTON_HEIGHT - 20.0f`

The coordinate system is rotated 90° (portrait coords mapped to landscape display).

### 3. Current UI Elements Visible
Looking at the screenshot:
- Green button at top-right: Shape texture button
- Purple/dark blue buttons: Grain texture button
- Black vertical bar on right side: Unknown (possibly scroll bar or other UI)
- Sliders at bottom: Brush size and opacity controls

### 4. Brush Loading Code
From `drawing_mobile_ios.mm:470-510`:
- `loadBrushesFromBundledFile()` loads from `/Users/pfeodrippe/dev/something/DrawingMobile/brushes.brushset`
- File exists (99MB confirmed)
- Logs should show "[BrushPicker] Loaded X brushes from dev path"

### 5. Thumbnail Loading Code
From `brush_importer.mm:357-394`:
- `loadThumbnailFromURL:` loads PNG as RGBA texture
- Uses `metal_stamp_load_rgba_texture_data()` to create Metal texture
- Stores in `brush.thumbnailTextureId`

### 6. Brush Picker Rendering
From `drawing_mobile_ios.mm:637-729`:
- `drawBrushPicker()` only draws when `picker.isOpen` is true
- Checks `brush->thumbnailTextureId > 0` for thumbnail display
- Falls back to shape texture or simple circle if no thumbnail

## Root Causes

1. **Brush button not visible**: The button position is in the bottom-right area but with rotated coordinates, it may not be easily accessible

2. **Brush picker not opening**: Even if button exists, taps at typical screen positions don't hit the button bounds

3. **No logs visible**: The app startup logs aren't captured, so we can't verify if brushes loaded

## Proposed Solutions

### Solution 1: Reposition Brush Button
Move the brush button to a more visible, accessible location - near the top-right with the texture buttons, or create a dedicated toolbar.

### Solution 2: Add Pencil Icon Button
Create a more intentional brush picker toggle with:
- A visible pencil/brush icon
- Positioned in the main UI area (top-left or top-right)
- Clear visual feedback when tapped

### Solution 3: Verify Thumbnail Pipeline
Add debug logging to verify:
1. Brushes are being loaded
2. Thumbnails are being found in QuickLook folders
3. RGBA textures are being created successfully
4. Textured rect pipeline is working

## Files to Modify

1. `DrawingMobile/drawing_mobile_ios.mm`
   - Fix brush button positioning
   - Add pencil icon texture loading
   - Add more visible brush picker toggle

2. `src/vybe/app/drawing/native/metal_renderer.mm` (already done)
   - RGBA texture loading: DONE
   - Textured rect rendering: DONE

## Commands to Test

```bash
# Build and run
make drawing-ios-jit-sim-run

# Check logs for brush loading
# Look for "[BrushPicker]" and "[BrushImporter]" messages
```

## Next Steps

1. Fix brush button position to be visible and accessible
2. Add visual indicator (could use texture or colored rect)
3. Test brush picker opening
4. Verify thumbnail textures are loading
5. Debug textured rect rendering if needed
