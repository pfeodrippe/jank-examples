# Brush Selection Fix Plan - January 6, 2026

## Problem Statement
When selecting a brush from the picker (e.g., "12 brushes of christmas"), the brush is not applied to the drawing. The stroke still looks the same regardless of which brush is selected.

## Root Cause Analysis

### Issue 1: Hardcoded Brush Settings Override Applied Brush

**Location**: `drawing_mobile_ios.mm:1333-1355`

The code flow is:
1. Line 1333: `loadBrushesFromBundledFile()` is called, which internally calls `[BrushImporter applyBrush:g_selectedBrushId]`
2. Lines 1340-1355: Hardcoded "Huntsman Crayon" settings **IMMEDIATELY OVERRIDE** the applied brush:
   ```cpp
   metal_stamp_set_brush_type(1);  // Crayon brush!
   metal_stamp_set_brush_hardness(0.35f);  // Crayon hardness
   metal_stamp_set_brush_spacing(0.06f);  // Tight spacing
   metal_stamp_set_brush_grain_scale(1.8f);
   metal_stamp_set_brush_size_pressure(0.8f);
   metal_stamp_set_brush_opacity_pressure(0.3f);
   metal_stamp_set_brush_scatter(3.0f);
   metal_stamp_set_brush_size_jitter(0.15f);
   metal_stamp_set_brush_opacity_jitter(0.1f);
   ```

**Fix**: Move brush loading AFTER the hardcoded defaults, OR remove the hardcoded settings and let the applied brush define the behavior.

### Issue 2: Touch Coordinate Mismatch (Potential)

**Location**: `drawing_mobile_ios.mm:1514-1524`

Touch events come in with coordinates that may not match where UI elements are rendered due to landscape rotation. Previous debug logs showed:
- Touch at simulator coordinates → transformed to app coordinates
- But brush picker rendered at different coordinates

The `getBrushAtPoint()` function (line 1516) might return -1 because touch coordinates don't align with the brush cell positions.

**Fix**: Verify coordinate transformation is consistent between touch handling and UI rendering.

### Issue 3: `applyBrush` Doesn't Set Brush Type

**Location**: `brush_importer.mm:648-674`

The `applyBrush` method sets:
- spacing, sizeJitter, opacityJitter, scatterAmount, rotationJitter
- sizePressure, opacityPressure, grainScale, hardness
- shapeTextureId, grainTextureId

But it does NOT set:
- `metal_stamp_set_brush_type()` - the brush type/mode

**Fix**: Add brush type setting to `applyBrush` or ensure imported brushes have a type field.

## Files to Modify

### 1. `DrawingMobile/drawing_mobile_ios.mm`

**Change A** - Move hardcoded defaults BEFORE brush loading:
```cpp
// Set DEFAULT brush settings first (will be overridden by loaded brush)
metal_stamp_set_brush_type(1);
metal_stamp_set_brush_hardness(0.35f);
// ... other defaults ...

// Load brushes - this will override defaults with actual brush settings
loadBrushesFromBundledFile();

// Only set size/opacity from sliders (these are user-controlled)
metal_stamp_set_brush_size(brushSize);
metal_stamp_set_brush_opacity(brushOpacity);
metal_stamp_set_brush_color(...);
```

**Change B** - Add debug logging to confirm brush selection:
```cpp
if (brushIdx >= 0) {
    g_selectedBrushIndex = brushIdx;
    g_selectedBrushId = [[g_brushIds objectAtIndex:brushIdx] intValue];
    [BrushImporter applyBrush:g_selectedBrushId];

    // DEBUG: Confirm what was applied
    ImportedBrush* brush = [BrushImporter getBrushById:g_selectedBrushId];
    if (brush) {
        NSLog(@"[BrushPicker] Applied brush '%s' - shape:%d grain:%d spacing:%.3f",
              brush->name, brush->shapeTextureId, brush->grainTextureId, brush->settings.spacing);
    }
    brushPicker.isOpen = false;
}
```

### 2. `DrawingMobile/brush_importer.mm`

**Change C** - Add brush type to applyBrush:
```cpp
+ (BOOL)applyBrush:(int32_t)brushId {
    ImportedBrush* brush = [self getBrushById:brushId];
    if (!brush) return NO;

    // Set brush type based on brush properties
    // Type 0 = basic, Type 1 = textured/crayon, etc.
    int brushType = (brush->shapeTextureId >= 0 || brush->grainTextureId >= 0) ? 1 : 0;
    metal_stamp_set_brush_type(brushType);

    // ... rest of settings ...
}
```

## Implementation Steps

1. **Remove/relocate hardcoded brush settings** in `drawing_mobile_ios.mm`
   - Keep only size, opacity, and color as user-controlled
   - Let `applyBrush` handle all other settings

2. **Add brush type setting** to `brush_importer.mm:applyBrush`

3. **Add debug logging** to confirm brush is being applied

4. **Test** with `make drawing-ios-jit-sim-run`:
   - Select different brushes
   - Verify stroke appearance changes
   - Check console logs for confirmation

## Commands

```bash
# Build and run
cd /Users/pfeodrippe/dev/something
make drawing-ios-jit-sim-run

# Or incremental build after .mm changes
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' build

# Install and launch
xcrun simctl terminate 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
xcrun simctl install 'iPad Pro 13-inch (M4)' ~/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-*/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

## Expected Outcome

After the fix:
1. Default brush loads and applies correctly on startup
2. Selecting a different brush from picker changes stroke appearance
3. Each brush's unique texture (shape/grain) and settings are reflected in strokes
