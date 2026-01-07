# Custom Texture Support Implementation Plan

## Research Summary

### How Procreate Brush Textures Work

Procreate brushes use two texture types:

1. **Shape Texture** - The outer form/container of the brush stamp
   - Determines the brush tip silhouette
   - A single tap creates a "stamp" with this shape
   - Dragging creates a stroke composed of multiple stamps

2. **Grain Texture** - The internal texture that fills the shape
   - Acts like a "paint roller" rolling texture inside the shape
   - Two behavior modes:
     - **Moving**: Grain drags/smears with stroke (roller locked in place)
     - **Texturized**: Grain stays static behind stroke (roller keeps rolling)

### Texture Format Requirements

| Property | Requirement |
|----------|-------------|
| Aspect Ratio | 1:1 (square) - Procreate auto-stretches non-square |
| Recommended Sizes | 512×512, 1024×1024, 2048×2048 (power of 2) |
| Color Format | Grayscale (White = paint, Black = transparent) |
| Grain Tiling | Seamless patterns recommended |
| File Format | PNG (with alpha) or any image format |

### Procreate .brush File Structure

`.brush` and `.brushset` files are ZIP archives containing:
- `Shape.png` - Shape texture
- `Grain.png` - Grain texture
- `Brush.archive` - Binary plist with all brush settings
- Preview images and metadata

Sources:
- [Procreate Handbook - Brush Studio Settings](https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings)
- [Design Bundles - How to Make Procreate Brushes](https://designbundles.net/design-school/how-to-make-procreate-brushes)
- [FileInfo - BRUSHSET Extension](https://fileinfo.com/extension/brushset)

---

## Current Implementation Status

### What Already Exists

The Metal renderer already has texture infrastructure:

```cpp
// metal_renderer.mm - Existing texture API
int32_t load_texture(const char* path);                           // Load from file
int32_t load_texture_from_data(const uint8_t* data, int w, int h); // Load raw data
void set_brush_shape_texture(int32_t texture_id);
void set_brush_grain_texture(int32_t texture_id);
void set_brush_grain_scale(float scale);
void set_brush_grain_moving(bool moving);
void unload_texture(int32_t texture_id);
```

### Shader Support

`stamp_shaders.metal` has `stamp_textured_fragment` shader for custom textures.

### Current Limitations

1. No UI for texture selection/upload
2. No iOS photo library integration
3. No persistent texture storage
4. No .brush file import
5. Textures loaded from files only (no runtime image picker)

---

## Implementation Plan

### Phase 1: Basic Texture Upload (Core Feature)

#### 1.1 Add Photo Library Picker (iOS)

**Files to modify:**
- `DrawingMobile/drawing_mobile_ios.mm`

**Implementation:**
```objc
// Add PHPickerViewController integration
- (void)showTexturePicker:(TextureType)type {
    PHPickerConfiguration *config = [[PHPickerConfiguration alloc] init];
    config.selectionLimit = 1;
    config.filter = [PHPickerFilter imagesFilter];

    PHPickerViewController *picker = [[PHPickerViewController alloc] initWithConfiguration:config];
    picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}
```

**Required frameworks:**
- `PhotosUI.framework`
- `Photos.framework` (for permission handling)

#### 1.2 Image Processing Pipeline

Convert selected image to grayscale square texture:

```cpp
// New function in metal_renderer.mm
int32_t load_texture_from_uiimage(UIImage* image, bool forShape) {
    // 1. Get CGImage
    CGImageRef cgImage = image.CGImage;

    // 2. Determine target size (power of 2, max 1024)
    size_t maxDim = MAX(CGImageGetWidth(cgImage), CGImageGetHeight(cgImage));
    size_t targetSize = 512;
    if (maxDim > 512) targetSize = 1024;
    if (maxDim > 1024) targetSize = 2048;

    // 3. Create square grayscale context
    CGColorSpaceRef graySpace = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(NULL, targetSize, targetSize,
                                              8, targetSize, graySpace,
                                              kCGImageAlphaNone);

    // 4. Draw image centered/scaled to fit square
    CGRect drawRect = AspectFitRect(imageSize, targetSize);
    CGContextSetFillColorWithColor(ctx, [UIColor blackColor].CGColor);
    CGContextFillRect(ctx, CGRectMake(0, 0, targetSize, targetSize));
    CGContextDrawImage(ctx, drawRect, cgImage);

    // 5. Get pixel data
    uint8_t* pixels = (uint8_t*)CGBitmapContextGetData(ctx);

    // 6. Load into Metal texture
    int32_t textureId = load_texture_from_data(pixels, targetSize, targetSize);

    // 7. Cleanup
    CGContextRelease(ctx);
    CGColorSpaceRelease(graySpace);

    return textureId;
}
```

#### 1.3 UI for Texture Selection

Add texture selection UI to the drawing app:

**Option A: Texture Picker Panel**
- Add "Shape" and "Grain" buttons near brush settings
- Tap opens photo picker
- Selected texture shown as thumbnail

**Option B: Quick Access Toolbar**
- Swipe gesture or button reveals texture options
- Built-in presets + "Import" button

**Recommended UI location:** Near the color picker button (bottom of screen)

### Phase 2: Texture Management

#### 2.1 Persistent Texture Storage

Store user textures in app documents:

```
Documents/
  Textures/
    Shapes/
      user_shape_001.png
      user_shape_002.png
    Grains/
      user_grain_001.png
      user_grain_002.png
    metadata.json  // Stores texture names, dates, usage stats
```

#### 2.2 Texture Library UI

- Grid view of saved textures
- Tap to select, long-press for options (delete, rename)
- Swipe to switch between Shapes/Grains tabs

#### 2.3 Built-in Texture Presets

Include common textures as app resources:

```
Shapes:
- Round (procedural - already exists)
- Square
- Ink Splatter
- Charcoal
- Spray

Grains:
- Paper Fine
- Paper Rough
- Canvas
- Noise
- Hatching
```

### Phase 3: Advanced Features (Optional)

#### 3.1 Procreate .brush Import

Parse Procreate brush files:

```cpp
bool import_procreate_brush(const char* brushPath) {
    // 1. Unzip .brush file to temp directory
    // 2. Extract Shape.png -> load as shape texture
    // 3. Extract Grain.png -> load as grain texture
    // 4. Parse Brush.archive (bplist) for settings
    // 5. Apply brush settings (size, spacing, opacity, etc.)
    return true;
}
```

**Dependencies:** libzip or ZipArchive framework

#### 3.2 Auto-Repeat/Seamless Tile Generator

For grain textures, add seamless tiling option:

```cpp
// Generate seamless tile from source image
uint8_t* make_seamless_tile(uint8_t* source, int size) {
    // Mirror-blend edges for seamless tiling
    // Similar to Procreate's "Auto Repeat" feature
}
```

#### 3.3 Texture Preview

Show real-time preview of how texture affects brush:
- Live stroke preview as user adjusts settings
- Mini canvas showing texture applied to sample stroke

---

## API Design

### New C API Functions

```c
// Texture loading from iOS image
METAL_EXPORT int32_t metal_stamp_load_texture_from_rgba(
    const uint8_t* data, int width, int height, bool convertToGrayscale);

// Texture management
METAL_EXPORT int32_t metal_stamp_get_shape_texture();
METAL_EXPORT int32_t metal_stamp_get_grain_texture();
METAL_EXPORT void metal_stamp_list_textures(int32_t* ids, int* count);

// Optional: Procreate import
METAL_EXPORT bool metal_stamp_import_procreate_brush(const char* path);
```

### UI Event Handlers

```cpp
// In drawing_mobile_ios.mm event loop
case CUSTOM_EVENT_SHAPE_TEXTURE_SELECTED: {
    int32_t textureId = loadTextureFromPickerResult(event.data);
    metal_stamp_set_brush_shape_texture(textureId);
    break;
}

case CUSTOM_EVENT_GRAIN_TEXTURE_SELECTED: {
    int32_t textureId = loadTextureFromPickerResult(event.data);
    metal_stamp_set_brush_grain_texture(textureId);
    break;
}
```

---

## Implementation Order

### Sprint 1 (Core)
1. [ ] Add PhotosUI framework to Xcode project
2. [ ] Implement image picker delegate in drawing_mobile_ios.mm
3. [ ] Add `load_texture_from_uiimage` function
4. [ ] Add UI buttons for Shape/Grain texture picker
5. [ ] Test with custom images

### Sprint 2 (Polish)
6. [ ] Add texture persistence (save to Documents)
7. [ ] Create texture library UI
8. [ ] Bundle built-in texture presets
9. [ ] Add texture thumbnails in UI

### Sprint 3 (Advanced - Optional)
10. [ ] Procreate .brush file import
11. [ ] Seamless tile generator
12. [ ] Real-time preview

---

## Testing Plan

1. **Basic Upload Test**
   - Import grayscale PNG as shape texture
   - Import grayscale PNG as grain texture
   - Verify brush uses textures correctly

2. **Format Handling**
   - Test color images (should convert to grayscale)
   - Test non-square images (should crop/fit to square)
   - Test various sizes (128px to 4096px)

3. **Edge Cases**
   - Very small textures (< 64px)
   - Very large textures (> 2048px)
   - Transparent images
   - Images with alpha channel

4. **Performance**
   - Texture loading time
   - Memory usage with multiple textures
   - Drawing performance with textured brush

---

## Files to Modify

| File | Changes |
|------|---------|
| `DrawingMobile/drawing_mobile_ios.mm` | Photo picker, UI buttons, event handling |
| `DrawingMobile/DrawingMobile-JIT-Sim.xcodeproj` | Add PhotosUI framework |
| `src/vybe/app/drawing/native/metal_renderer.mm` | `load_texture_from_uiimage()` |
| `src/vybe/app/drawing/native/metal_renderer.h` | New API declarations |
| `Makefile` | Update build flags if needed |

---

## Estimated Timeline

- **Phase 1 (Core)**: 2-4 hours
- **Phase 2 (Polish)**: 4-6 hours
- **Phase 3 (Advanced)**: 6-8 hours (optional)

Total for basic feature: **2-4 hours**
