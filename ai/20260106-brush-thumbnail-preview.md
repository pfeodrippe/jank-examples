# Brush Thumbnail Preview Implementation - January 6, 2026

## What I Learned

### Procreate Brush Thumbnails
- Procreate brushsets contain `QuickLook/Thumbnail.png` for each brush
- Format: 327x100 pixels, 8-bit grayscale+alpha PNG
- White brush stroke on transparent background - perfect for tinting
- Located at either root or `Sub01/QuickLook/Thumbnail.png`

### Metal Texture Management
- RGBA textures need `MTLPixelFormatRGBA8Unorm` format
- 4 bytes per pixel, stride = width * 4
- Separate pipeline needed for textured UI rects (vs solid color rects)
- Textured rect shader samples texture and multiplies by tint color

### Metal Shader for Textured UI
- Vertex shader identical to solid rect shader
- Fragment shader adds texture sampling with tint multiplication
- UV coordinates need Y-flip for proper texture orientation
- Uses sampler state from existing textureSampler property

## Files Modified

### DrawingMobile/jank-resources/.../metal_renderer.h
- Added `load_texture_from_rgba_data()` method to MetalStampRenderer class
- Added `queue_ui_textured_rect()` method for textured UI drawing
- Added extern "C" declarations for JIT integration

### DrawingMobile/jank-resources/.../metal_renderer.mm
- Added `loadTextureFromRGBAData:` Objective-C method
- Added `queueUITexturedRect:` method with NDC coordinate conversion
- Added `UITexturedRectParams` struct for shader parameters
- Added `uiTexturedRectPipeline` render pipeline state
- Added MSL shaders: `ui_textured_rect_vertex` and `ui_textured_rect_fragment`
- Updated `drawQueuedUIRectsToTexture:` to handle both solid and textured rects
- Added namespace and extern "C" wrapper functions

### DrawingMobile/brush_importer.h
- Added `thumbnailTextureId`, `thumbnailWidth`, `thumbnailHeight` to ImportedBrush struct

### DrawingMobile/brush_importer.mm
- Added `loadThumbnailFromURL:` method using RGBA context
- Updated import functions to load thumbnails from QuickLook folder
- Uses `metal_stamp_load_rgba_texture_data()` for RGBA textures

### DrawingMobile/drawing_mobile_ios.mm
- Updated `drawBrushPicker()` to display thumbnail textures
- Calculates aspect ratio to fit 327x100 thumbnails in picker grid
- Falls back to solid rect for brushes without thumbnails

## Commands I Ran

```bash
# Build iOS simulator
make drawing-ios-jit-sim-build 2>&1 | tee /tmp/build_output.txt
```

## What's Next

- Test thumbnail display in the brush picker UI
- Add brush names below thumbnails (requires text rendering or Metal font atlas)
- Implement scrolling for large brush collections
- Consider thumbnail caching for faster load times
