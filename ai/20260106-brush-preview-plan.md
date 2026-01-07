# Brush Preview Implementation Plan

## Discovery
- Procreate brushsets contain `QuickLook/Thumbnail.png` for each brush
- Format: 327x100 pixels, 8-bit grayscale+alpha PNG
- Shows the actual brush stroke preview (white stroke on transparent background)

## Implementation Steps

### 1. Update ImportedBrush struct
Add `thumbnailTextureId` field to store the loaded preview texture.

### 2. Update brush_importer.mm
- Load `QuickLook/Thumbnail.png` from each brush folder
- Load as Metal texture using `metal_stamp_load_texture_data()`
- Store texture ID in brush data

### 3. Add Metal function to draw textured UI rect
- Create `metal_stamp_queue_ui_textured_rect()` to draw preview textures
- Blend white texture on dark background

### 4. Update brush picker UI
- Draw thumbnail textures instead of solid rectangles
- Scale to fit picker item size
- Show brush name below thumbnail

## Files to Modify
- `DrawingMobile/brush_importer.h` - Add thumbnailTextureId
- `DrawingMobile/brush_importer.mm` - Load thumbnails
- `DrawingMobile/jank-resources/.../metal_renderer.mm` - Add textured rect function
- `DrawingMobile/drawing_mobile_ios.mm` - Update picker drawing
