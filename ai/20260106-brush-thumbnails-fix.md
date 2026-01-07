# Brush Thumbnails Fix - January 6, 2026

## What I Learned

### Root Cause of Gray Squares
The brush thumbnail previews were showing as gray squares because the **textured rect shader pipeline was nil**. The shaders `ui_textured_rect_vertex` and `ui_textured_rect_fragment` existed only in the fallback source string in `metal_renderer.mm` (lines 420-445), but were **NOT** in the pre-compiled `stamp_shaders.metal` file.

Since the app uses the default Metal library (which has `stamp_vertex`), it never falls back to source compilation, so the textured rect shaders were never available.

### Solution
Added the missing shaders to `stamp_shaders.metal`:
- `UITexturedRectParams` struct
- `ui_textured_rect_vertex` - generates quad vertices with UV coordinates
- `ui_textured_rect_fragment` - samples texture with tint color, flips Y for correct orientation

### Metal Shader Pipeline
- Metal compiles `.metal` files into the default library at build time
- Fallback source compilation only happens if default library is missing
- All shaders used at runtime must be in the `.metal` file OR the fallback source
- `newFunctionWithName:` returns nil if shader function doesn't exist in library

### Brush Picker Configuration
Final working values:
```cpp
static const float BRUSH_PICKER_ITEM_SIZE = 180.0f;  // Larger for visibility
static const float BRUSH_PICKER_ITEM_GAP = 12.0f;
static const float BRUSH_PICKER_PADDING = 20.0f;
static const int BRUSH_PICKER_COLS = 4;
float brushPickerHeight = 800.0f;
```

## Commands I Ran

```bash
# Full rebuild (cleans everything)
cd /Users/pfeodrippe/dev/something && make drawing-ios-jit-sim-run

# Incremental build (after .mm changes only)
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' build

# Install and launch
xcrun simctl terminate 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
xcrun simctl install 'iPad Pro 13-inch (M4)' ~/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-*/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

## Files Modified

1. **`src/vybe/app/drawing/native/stamp_shaders.metal`** (lines 509-540)
   - Added `UITexturedRectParams` struct
   - Added `ui_textured_rect_vertex` shader
   - Added `ui_textured_rect_fragment` shader

2. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Adjusted brush picker item size from 100px to 180px
   - Changed columns from 4 to 4, gap to 12px
   - Increased picker height to 800px

## Key Insight
When debugging Metal rendering issues where geometry is drawn but textures don't appear:
1. Check if the render pipeline state is nil
2. Verify shader functions exist in the Metal library with `newFunctionWithName:`
3. Ensure shaders are in the `.metal` file that gets compiled, not just in fallback source

## What's Next
- Consider adding scrolling to brush picker for many brushes
- Add brush name labels under thumbnails
- Fix touch coordinate transformation for brush selection
