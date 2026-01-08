# shapeInverted Brush Texture Fix

## What I Learned

### Procreate shapeInverted Property
- Procreate brush archives contain a `shapeInverted` property that determines how shape texture alpha is interpreted
- **shapeInverted=0** (default): WHITE pixels in Shape.png = opaque brush strokes (white shapes on black background)
- **shapeInverted=1** (inverted): BLACK pixels in Shape.png = opaque brush strokes (black shapes on white background)

### Critical Discovery: Sub01 Folder Structure
Procreate brushes can have a **Sub01 subfolder** for dual-brush configurations. Key insight:
- **Root Brush.archive** and **Sub01/Brush.archive** can have DIFFERENT shapeInverted values!
- When Shape.png is loaded from Sub01/, the shapeInverted must come from Sub01/Brush.archive
- The fix: **always load Brush.archive from the same directory as Shape.png**

Example:
```
brush_folder/
├── Brush.archive          # shapeInverted=0
├── Sub01/
│   ├── Shape.png          # <-- texture loaded from here
│   └── Brush.archive      # shapeInverted=1 <-- must use THIS archive!
```

### Key Insight
When loading grayscale textures:
- CGBitmapContext with DeviceGray does NOT invert values
- WHITE (255) in PNG → texColor.r = 1.0 in shader
- BLACK (0) in PNG → texColor.r = 0.0 in shader

### Correct Shader Logic
```metal
if (uniforms.shapeInverted == 0) {
    shapeAlpha = texColor.r;         // WHITE=opaque: use value directly
} else {
    shapeAlpha = 1.0 - texColor.r;   // BLACK=opaque: invert value
}
```

### Debugging Process
1. Viewed actual Shape.png textures to understand conventions
2. Checked Brush.archive files with `plutil -p` to read shapeInverted values
3. Discovered ROOT vs Sub01 archive mismatch - different shapeInverted values!
4. Fixed by loading archive from same directory as the Shape.png

## Commands I Ran

```bash
# Find brush archives in simulator
find ~/Library/Developer/CoreSimulator -name "Brush.archive" | head -20

# Check shapeInverted value in brush archive
cat "path/to/Brush.archive" | plutil -p - | grep shapeInverted

# Compare root vs Sub01 archives - KEY DEBUGGING STEP!
plutil -p "brush_folder/Brush.archive" | grep shapeInverted
plutil -p "brush_folder/Sub01/Brush.archive" | grep shapeInverted

# Copy Shape.png for visual inspection
cp "path/to/Shape.png" ~/Desktop/brush_shape.png

# Check image properties
sips -g hasAlpha -g space -g samplesPerPixel "brush_shape.png"

# Build and run Drawing iOS app
make drawing-ios-jit-sim-build 2>&1 | tee /tmp/drawing_build.txt
```

## Files Modified

1. **stamp_shaders.metal** (line ~262-271)
   - Fixed conditional logic for shapeInverted in `stamp_textured_fragment` shader
   - shapeInverted=0 now uses `texColor.r` directly
   - shapeInverted=1 now uses `1.0 - texColor.r`

2. **brush_importer.mm**
   - Added parsing of `shapeInverted` property from Brush.archive
   - **CRITICAL FIX**: Load Brush.archive from same directory as Shape.png
   - Added call to `metal_stamp_set_brush_shape_inverted()` in applyBrush

3. **brush_importer.h**
   - Added `int shapeInverted` to `ProcreateBrushSettings` struct

4. **metal_renderer.h**
   - Added `int shape_inverted` to `BrushSettings` struct
   - Added `set_brush_shape_inverted()` method declaration
   - Added `metal_stamp_set_brush_shape_inverted()` C export

5. **metal_renderer.mm**
   - Added `int shapeInverted` to `MSLStrokeUniforms` struct
   - Added `shapeInverted` property to `MetalStampRendererImpl`
   - Implemented setter and uniform passing

---

## Brush Picker Coordinate Fix (Additional Issue)

### Problem
When selecting brushes in the picker, tapping on the 5th brush (row 1, col 0) would select "random" brushes - actually different brushes each tap.

### Root Cause: Y Coordinate System Mismatch
- **Metal UI rendering**: Y=0 at BOTTOM of screen, Y increases UPWARD
- **SDL touch events**: Y=0 at TOP of screen, Y increases DOWNWARD

The brush picker was rendered with row 0 at the bottom, but touch coordinates assumed row 0 at the top. This caused:
- Tapping visual row 1 → code calculates row 2 or 3
- Small finger position variations → different calculated rows
- Appears "random" because visual boundaries don't match hit zones

### The Fix
Transform touch Y to Metal Y before brush picker calculations:
```cpp
// In SDL_EVENT_FINGER_DOWN handler:
} else if (isPointInBrushPicker(brushPicker, x, height - y)) {
    float metalY = height - y;
    int brushIdx = getBrushAtPoint(brushPicker, x, metalY);
    // ...
}
```

### Files Modified (REVERTED - wrong fix)
- **drawing_mobile_ios.mm** - Y coordinate transform was wrong approach

---

## ACTUAL Root Cause: Unordered Directory Listing

### Problem
`contentsOfDirectoryAtURL` returns files in **unspecified filesystem order** that can vary between calls. This means brushes are loaded in random order, so brush index 5 in the picker could map to different actual brushes!

### The Fix
Sort directory contents by filename before iterating:
```objc
// IMPORTANT: Sort by filename to ensure consistent brush ordering
contents = [contents sortedArrayUsingComparator:^NSComparisonResult(NSURL* a, NSURL* b) {
    return [[a lastPathComponent] compare:[b lastPathComponent]];
}];
```

### Files Modified
- **brush_importer.mm** (line ~561-565)
  - Added sorting of directory contents before importing brushes

## What's Next

- Test with more brush types to ensure all work correctly
- The Sub01 folder is a Procreate convention for dual brushes
- Consider handling other Sub folders if they exist (Sub02, etc.)
- **Test brush picker selection is now consistent**
