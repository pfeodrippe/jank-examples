# .vybed File Format Implementation

## Summary

Implemented a custom file format (.vybed) for saving Vybe drawing animations, along with a gallery screen for browsing saved drawings.

## What I Learned

### 1. Custom Binary File Format Design

- **Hybrid approach**: JSON header (human-readable metadata) + binary stroke data (compact)
- **Version field**: Magic number "VYBED001" for format evolution
- **Structure**:
  ```
  [MAGIC 8 bytes] [HEADER_SIZE u32] [JSON header] [THUMB_SIZE u32] [PNG thumb] [stroke data]
  ```

### 2. iOS File Storage

- **Documents directory**: Use `NSDocumentDirectory` for user-visible files
- **Backed up automatically**: Documents are synced to iCloud by default
- **FileManager APIs**: `NSFileManager` for directory creation, file listing
- **Sort by modification date**: Gallery shows newest first

### 3. C++ Serialization

- **Little-endian binary**: Standard for cross-platform
- **Per-thread stroke data**: Each thread's frames serialized contiguously
- **Brush data included**: Full StrokeBrush struct preserved
- **Bounding boxes recalculated**: On load, `updateBounds()` called for each stroke

## Files Created

| File | Purpose |
|------|---------|
| `src/vybe/app/drawing/native/vybed_format.hpp` | Format definitions, API declarations |
| `src/vybe/app/drawing/native/vybed_format.cpp` | Serialization implementation |
| `src/vybe/app/drawing/native/file_manager_ios.h` | iOS file operations API |
| `src/vybe/app/drawing/native/file_manager_ios.mm` | Objective-C++ implementation |
| `ai/20260111-vybed-file-format.md` | Format specification document |

## Files Modified

| File | Changes |
|------|---------|
| `DrawingMobile/config-common.yml` | Added vybed_format.cpp, file_manager_ios.mm to sources |
| `DrawingMobile/drawing_mobile_ios.mm` | Added gallery button, panel, and touch handling |

## Key Code Patterns

### Save Drawing
```cpp
// In file_manager_ios.mm
const char* ios_save_drawing(const char* name) {
    if (!ios_ensure_drawings_directory()) return NULL;
    std::string filename = vybe::drawing::generate_vybed_filename();
    std::string fullPath = std::string(ios_get_drawings_path()) + "/" + filename;
    auto& weave = animation::getCurrentWeave();
    bool success = vybe::drawing::save_vybed(fullPath, weave, name, nullptr, 0);
    ...
}
```

### Load Drawing
```cpp
int ios_load_drawing(const char* path) {
    auto& weave = animation::getCurrentWeave();
    std::string name;
    bool success = vybe::drawing::load_vybed(path, weave, name);
    if (success) {
        weave.invalidateAllCaches();
    }
    return success ? 1 : 0;
}
```

### Gallery Touch Handling
```cpp
if (g_gallery.isOpen) {
    // Calculate panel bounds
    float panelW = width * 0.85f;
    ...
    // Check close button, new button, item taps
    int itemIdx = getGalleryItemAtPoint(...);
    if (itemIdx >= 0) {
        ios_load_drawing(g_gallery.filePaths[itemIdx].c_str());
        g_gallery.isOpen = false;
    }
}
```

## Commands Run

```bash
# Regenerate Xcode project
cd DrawingMobile && ./generate-project.sh project-jit-sim.yml

# Build
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj \
  -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
  build
```

## Auto-Save Implementation (Completed)

### SDL Event Watcher
```cpp
// SDL3 requires background events via SDL_AddEventWatch (not event loop)
static bool sdlEventWatcher(void* userdata, SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            NSLog(@"[AppLifecycle] WILL_ENTER_BACKGROUND - auto-saving...");
            autoSaveCurrentDrawing();
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            refreshGalleryList();
            break;
    }
    return true;
}

// Register after SDL window creation
SDL_AddEventWatch(sdlEventWatcher, nullptr);
```

### Auto-Save Logic
- Tracks `currentDrawingPath` (empty = new drawing, path = loaded drawing)
- Tracks `hasUnsavedChanges` flag (set true after pen stroke ends)
- On background: saves to existing file or creates new .vybed
- On foreground: refreshes gallery list

### Testing Results
```
[AppLifecycle] WILL_ENTER_BACKGROUND - auto-saving...
[AutoSave] No content to save  // Empty canvas correctly detected
[AppLifecycle] DID_ENTER_BACKGROUND
```

## What's Next

1. ~~**Auto-save**: Save drawing automatically when app backgrounds~~ ✓ DONE
2. ~~**Thumbnail generation**: Capture Metal texture as PNG for gallery preview~~ ✓ DONE
3. **Text rendering**: Add actual file names to gallery items (pending)
4. ~~**Gallery scrolling**: Handle scroll gesture for many drawings~~ ✓ DONE
5. ~~**Delete confirmation**: Add swipe-to-delete~~ ✓ DONE
6. ~~**Rename functionality**: Allow renaming drawings from gallery~~ ✓ DONE

## Gallery Enhancements (Session 2)

### Thumbnail Generation
```cpp
// Capture canvas as PNG thumbnail (200x160)
int ios_capture_thumbnail(unsigned char* buffer, int buffer_size) {
    // 1. Get RGBA pixels from Metal canvas
    unsigned char* pixels = nullptr;
    int pixelDataSize = metal_stamp_capture_snapshot(&pixels);

    // 2. Create CGImage -> scale to thumbnail -> encode PNG
    CGContextRef context = CGBitmapContextCreate(pixels, ...);
    CGImageRef cgImage = CGBitmapContextCreateImage(context);

    // 3. Scale to 200x160
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(200, 160), NO, 1.0);
    CGContextDrawImage(thumbContext, CGRectMake(0, 0, 200, 160), cgImage);
    UIImage* thumbImage = UIGraphicsGetImageFromCurrentImageContext();

    // 4. Return PNG data
    NSData* pngData = UIImagePNGRepresentation(thumbImage);
    memcpy(buffer, [pngData bytes], pngSize);
    return pngSize;
}
```

### Gallery Scrolling
- **Vertical scroll**: Drag up/down to scroll through drawings
- **Scroll offset tracking**: `g_gallery.scrollOffset` applied to item Y positions
- **Content height clamping**: `maxScroll = max(0, contentHeight - viewportH)`
- **Off-screen culling**: Items outside viewport are skipped for rendering

### Swipe-to-Delete
- **Swipe left**: Reveals red delete button (80px wide)
- **Tap delete button**: Calls `ios_delete_drawing()` and refreshes gallery
- **Threshold**: 50px swipe required to reveal button
- **Snap animation**: Button snaps to fully revealed position

### Swipe-to-Rename
- **Swipe right**: Reveals blue rename button on the left
- **Tap rename button**: Shows native iOS `UIAlertController` with text field
- **Rename callback**: Updates file metadata, refreshes gallery

```cpp
// Show native iOS rename dialog
static void showRenameDialog(int itemIndex) {
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Rename Drawing"
                                                                   message:nil
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addTextFieldWithConfigurationHandler:^(UITextField* textField) {
        textField.text = currentNameStr;
    }];

    UIAlertAction* renameAction = [UIAlertAction actionWithTitle:@"Rename"
                                                           style:UIAlertActionStyleDefault
                                                         handler:^(UIAlertAction* action) {
        ios_rename_drawing(filePath.c_str(), [newName UTF8String]);
        refreshGalleryList();
    }];

    [rootVC presentViewController:alert animated:YES completion:nil];
}
```

### GalleryState Structure
```cpp
struct GalleryState {
    bool isOpen;
    std::vector<std::string> filePaths;
    std::vector<std::string> fileNames;
    std::vector<int32_t> thumbnailTextureIds;  // Metal texture IDs
    float scrollOffset;

    // Drag tracking
    bool isDragging;
    float dragStartX, dragStartY;
    SDL_FingerID dragFingerId;

    // Swipe-to-delete/rename
    int swipedItemIndex;       // -1 = none
    float swipeOffsetX;        // Negative = delete, Positive = rename
    bool showDeleteButton;
};
```

## Architecture Decisions

1. **Binary vs ZIP**: Chose binary for simplicity (no ZIP library needed)
2. **Save button removed**: User requested seamless auto-save instead
3. **Gallery as initial screen**: Opens on app launch to show saved drawings
4. **JSON header**: Human-readable metadata makes debugging easier

## Testing Notes

- Build succeeded with only warnings (unused functions from previous code)
- Gallery panel renders as overlay on canvas
- Touch handling distinguishes close button, new button, and item selection
- File listing sorted by modification date (newest first)

### Bug Fixes During Testing
- **Brush picker blocking gallery**: Changed `brushPicker.isOpen = true` to `false` (was set to true for testing)
- **Missing incremental.pch**: Copied `incremental-device.pch` to `incremental.pch` for simulator build

### Commands for Testing
```bash
# Build and run in simulator
make drawing-ios-jit-sim-run

# Or manual build/install
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj \
  -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator build

xcrun simctl install booted /path/to/DrawingMobile-JIT-Sim.app
xcrun simctl launch booted com.vybe.DrawingMobile-JIT-Sim

# Check auto-save logs
xcrun simctl spawn booted log show --predicate 'eventMessage CONTAINS "AutoSave"' --last 1m
```
