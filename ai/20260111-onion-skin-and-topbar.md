# Onion Skin Implementation & Procreate-Style Top Bar

## What I Learned

### 1. Onion Skin Animation Technique
- Shows previous/next animation frames as semi-transparent overlays
- Previous frames tinted reddish, next frames tinted bluish
- Opacity fades for frames further from current
- Paper background filtered out (brightness > 0.92 = transparent)

### 2. Metal Rendering Pipeline for Overlays
- Created separate `onionSkinPipeline` with alpha blending enabled
- BlendFactor: sourceAlpha/oneMinusSourceAlpha for proper compositing
- Uniforms struct matches MSL shader exactly (padding critical for alignment)

### 3. Frame Cache Integration
- Used existing `frameCacheTextures` (12 pre-allocated GPU textures)
- Current frame index from `currentFrameIndex` to calculate offsets
- Looping logic for animation wrap-around

### 4. UI Layout in iOS (Coordinate System)
- Screen is rotated 90 degrees for landscape
- `height` dimension is actually the horizontal width visible
- Top bar positioned at `height - TOP_BAR_HEIGHT - margin`

## Commands I Ran

```bash
# Build and run iOS JIT simulator
make drawing-ios-jit-sim-run

# Check for build errors
grep -i "error\|failed" /tmp/ios_build.txt
```

## Files Modified

### Metal Renderer (C++/ObjC)
- `src/vybe/app/drawing/native/metal_renderer.mm`:
  - Added onion skin properties to MetalStampRendererImpl
  - Created OnionSkinUniforms struct
  - Added `createOnionSkinPipeline` method
  - Added `drawOnionSkinFramesToTexture` compositing method
  - Modified `present()` to integrate onion skin rendering
  - Added public class methods for onion skin settings
  - Added C API exports (metal_stamp_set_onion_skin_*)
  - Enabled onion skin by default (YES)

- `src/vybe/app/drawing/native/metal_renderer.h`:
  - Added onion skin method declarations to MetalStampRenderer class
  - Added C API function declarations

### Metal Shaders
- `src/vybe/app/drawing/native/stamp_shaders.metal`:
  - Added OnionSkinUniforms struct
  - Added `onion_skin_vertex` shader
  - Added `onion_skin_fragment` shader with paper filtering

### iOS Drawing App
- `DrawingMobile/drawing_mobile_ios.mm`:
  - Added TOP_BAR constants (HEIGHT=70, BUTTON_SIZE=50)
  - Repositioned buttons to horizontal top bar layout
  - Updated `drawBrushButton` for compact square icon
  - Updated `drawEraserButton` for compact icon
  - Updated `drawOnionSkinButton` for compact icon
  - Added `drawTopBar` function for dark background
  - Added touch handling for onion skin button
  - Added FourFingerGesture struct (legacy, button preferred)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ TOP BAR (70px height, semi-transparent dark)                │
│ [Onion Skin]                       [Brush][Eraser][Color]   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  [Size Slider]                                              │
│  [Opacity Slider]                    CANVAS                 │
│  [Color Button]                                             │
│                                                             │
│                                     [Frame Wheel]           │
└─────────────────────────────────────────────────────────────┘
```

## Onion Skin Rendering Pipeline

```
present() {
    1. Draw onion skin frames (if enabled)
       - For each previous frame (2 by default): composite with red tint
       - For each next frame (1 by default): composite with blue tint
    2. Draw canvas with preserved content (MTLLoadActionLoad)
    3. Draw current stroke
    4. Draw UI elements
    5. Present to screen
}
```

## Default Onion Skin Settings

```objc
self.onionSkinEnabled = YES;           // ON by default
self.onionSkinPrevCount = 2;           // Show 2 previous frames
self.onionSkinNextCount = 1;           // Show 1 next frame
self.onionSkinOpacity = 0.35f;         // 35% base opacity
self.onionSkinPrevColor = (1.0, 0.4, 0.4, 1.0);  // Reddish
self.onionSkinNextColor = (0.4, 0.6, 1.0, 1.0);  // Bluish
```

## What's Next

1. Test onion skin on physical device
2. Fine-tune opacity and color tinting
3. Consider adding settings panel for onion skin (prev/next count, colors)
4. Move sliders to a popup/panel when brush is tapped
5. Add "Gallery" button to left side of top bar
