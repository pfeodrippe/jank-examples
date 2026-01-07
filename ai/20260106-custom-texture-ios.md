# Custom Texture Support for iOS Drawing App

**Date:** 2026-01-06

## What I Learned

### 1. iOS Photo Picker Integration (PHPickerViewController)
- `PHPickerViewController` is the modern iOS API for selecting photos (replaces deprecated `UIImagePickerController`)
- Requires `Photos` and `PhotosUI` frameworks in Xcode project
- Works asynchronously with delegate pattern (`PHPickerViewControllerDelegate`)
- Selected images are provided as `NSItemProvider` objects that need async loading

### 2. Coordinate System Rotation on iOS
**Critical Discovery:** The SDL window on iPad in landscape mode has a 90° rotated coordinate system!

- Window dimensions reported as portrait (e.g., 2752x2064) even when displaying landscape
- Metal rendering uses portrait coordinates
- Touch events use portrait coordinates
- But visual display is rotated 90° clockwise

**Transformation formula discovered:**
```
appX = windowWidth - 2 * simulatorY
appY = 2 * simulatorX (clamped to max windowHeight)
```

This means:
- To tap at app coordinate (x, y), use simulator coordinates:
  - simX = appY / 2
  - simY = (windowWidth - appX) / 2

### 3. Texture Button Implementation
- Created `TextureButtonConfig` struct for Shape and Grain texture buttons
- Buttons change color when texture is loaded (visual feedback)
- Hit detection uses `isPointInTextureButton()` function
- Global variables track texture state (`g_shapeTextureHasTexture`, `g_shapeTextureId`, etc.)

### 4. Procreate Texture Format Research
- **Shape textures**: Define brush tip silhouette (1:1 square, power of 2 sizes)
- **Grain textures**: Define internal fill pattern
- Format: Grayscale PNG where white = paint, black = transparent
- `.brush` files are ZIP archives containing Shape.png, Grain.png, Brush.archive

## Commands I Ran

```bash
# Clean derived data (fixes CFBundleVersion install errors)
rm -rf ~/Library/Developer/Xcode/DerivedData/DrawingMobile-*

# Build iOS app
cd /Users/pfeodrippe/dev/something/DrawingMobile
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj \
  -scheme DrawingMobile-JIT-Sim \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
  build

# Install and launch on simulator
xcrun simctl install 'iPad Pro 13-inch (M4)' ~/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-*/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim

# View app logs (NSLog output)
xcrun simctl spawn 'iPad Pro 13-inch (M4)' log show --last 30s --predicate 'process == "DrawingMobile-JIT-Sim"'
```

## Files Modified

1. **`DrawingMobile/project-jit-sim.yml`** and **`project-jit-device.yml`**
   - Added `Photos` and `PhotosUI` frameworks

2. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Added `#import <Photos/Photos.h>` and `#import <PhotosUI/PhotosUI.h>`
   - Added `TextureType` enum (Shape, Grain)
   - Added global texture state variables
   - Added `TexturePickerDelegate` Objective-C class implementing `PHPickerViewControllerDelegate`
   - Added `showTexturePicker()` function
   - Added `TextureButtonConfig` struct and button drawing/hit detection
   - Added texture button tap handling in event loop

3. **`ai/20260106-custom-texture-implementation-plan.md`**
   - Implementation plan document

## What's Next

1. **Apply textures to brush rendering**: Currently textures load but need to verify they're being applied to the Metal brush shader
2. **Add texture preview thumbnails**: Show loaded texture on the button itself
3. **Persist texture selections**: Save/restore texture choices between sessions
4. **Support Procreate .brush import**: Parse ZIP files and extract texture images
5. **Better button positioning**: Current position at top-right works but could be improved for UX

## Key Code Patterns

### PHPickerViewController Setup
```objc
PHPickerConfiguration* config = [[PHPickerConfiguration alloc] init];
config.selectionLimit = 1;
config.filter = [PHPickerFilter imagesFilter];

PHPickerViewController* picker = [[PHPickerViewController alloc] initWithConfiguration:config];
picker.delegate = delegate;
[rootVC presentViewController:picker animated:YES completion:nil];
```

### Texture Loading Callback
```objc
- (void)picker:(PHPickerViewController *)picker didFinishPicking:(NSArray<PHPickerResult *> *)results {
    [picker dismissViewControllerAnimated:YES completion:nil];
    if (results.count == 0) return;

    PHPickerResult* result = results[0];
    [result.itemProvider loadDataRepresentationForTypeIdentifier:UTTypeImage.identifier
                                              completionHandler:^(NSData* data, NSError* error) {
        // Load texture from data...
    }];
}
```

### Coordinate Transformation (for MCP simulator taps)
```cpp
// To hit button at app coords (appX, appY) with window size (W, H):
float simX = appY / 2.0f;
float simY = (W - appX) / 2.0f;
```
