# Procreate Brush Import Feature - January 6, 2026

## What I Learned

### Procreate .brushset Format
- `.brushset` files are ZIP archives containing multiple brush folders
- Each brush folder contains:
  - `Brush.archive` - NSKeyedArchiver binary plist with brush settings
  - `Shape.png` - Optional brush shape texture
  - `Grain.png` - Optional grain/paper texture
  - `QuickLook/Thumbnail.png` - Brush preview thumbnail

### NSKeyedArchiver Plist Parsing
- Brush settings are stored in `$objects` array at index 1
- Key Procreate settings discovered:
  - `plotSpacing` - Brush stamp spacing (0-1)
  - `dynamicsJitterSize` - Random size variation
  - `dynamicsJitterOpacity` - Random opacity variation
  - `plotJitter` - Position scatter amount
  - `dynamicsPressureSize` - How pressure affects size (-1 to 1)
  - `dynamicsPressureOpacity` - How pressure affects opacity
  - `grainDepth` - Grain texture intensity
  - `dynamicsGlazedFlow` - Paint flow setting

### iOS Coordinate System
- 90-degree rotation between SDL touch events and Metal rendering
- Portrait coordinates (width x height) map to landscape display
- Touch coordinates must be transformed for UI hit testing

## Files Modified

### DrawingMobile/brush_importer.h
- Added `ProcreateBrushSettings` struct with brush parameters
- Added `ImportedBrush` struct for brush data storage
- Added `loadBundledBrushSet:` and `loadBrushSetFromPath:` methods

### DrawingMobile/brush_importer.mm
- Implemented ZIP extraction using manual deflate decompression
- Implemented NSKeyedArchiver plist parsing for brush settings
- Added texture loading via `metal_stamp_load_texture_data()`
- Fixed extern declarations to match metal_renderer functions

### DrawingMobile/drawing_mobile_ios.mm
- Added `BrushPickerConfig` struct and picker panel UI
- Added `drawBrushPicker()` function to render brush grid
- Added tap handling for brush picker selection
- Modified brush button to toggle picker panel open/closed

### DrawingMobile/config-common.yml
- Added `brush_importer.mm` to CommonSources template

## Commands I Ran

```bash
# Build and run iOS simulator
make drawing-ios-jit-sim-run

# Clean simulator build
make drawing-ios-jit-sim-clean

# Capture simulator logs
xcrun simctl spawn 'iPad Pro 13-inch (M4)' log stream --predicate 'processID == <PID>' --style compact

# Examine Procreate plist format
plutil -convert xml1 -o - Brush.archive | head -100
```

## What's Next

- Add brush thumbnails/previews in the picker grid
- Implement scrolling for large brush collections
- Add brush name labels in the picker
- Consider caching brush textures for faster switching
- Test with Apple Pencil for pressure dynamics
