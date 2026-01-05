# Metal Stamp Renderer Implementation

**Date**: 2025-01-05
**Status**: Initial Implementation Complete (Build Successful)

## What Was Done

### 1. Created Metal Shader File
- **File**: `src/vybe/app/drawing/native/stamp_shaders.metal`
- Contains MSL (Metal Shading Language) shaders for GPU stamp rendering:
  - `stamp_vertex`: Point sprite vertex shader
  - `stamp_fragment`: Soft circular brush with configurable hardness
  - `stamp_textured_fragment`: For custom brush textures
  - `stroke_quad_vertex/fragment`: Line segment expansion for continuous strokes
  - `clear_vertex/fragment`: Canvas clearing

### 2. Created Metal Renderer Interface
- **File**: `src/vybe/app/drawing/native/metal_renderer.h`
- C++ interface in `metal_stamp` namespace:
  - `MetalStampRenderer` class with brush settings, stroke management, canvas control
  - Global convenience functions: `init_metal_renderer()`, `metal_begin_stroke()`, etc.
  - `BrushSettings` struct: size, hardness, opacity, spacing, color

### 3. Created Metal Renderer Implementation
- **File**: `src/vybe/app/drawing/native/metal_renderer.mm`
- Objective-C++ implementation using Metal API:
  - `MetalStampRendererImpl` class at global scope (Objective-C requirement)
  - Point interpolation for smooth strokes
  - Alpha blending with Porter-Duff "over" compositing
  - Canvas texture for stroke accumulation
  - Fallback shader compilation from source string

### 4. Updated Build Configuration
- **File**: `DrawingMobile/config-common.yml`
  - Added `metal_renderer.mm` as compiled source with `-fobjc-arc`
  - Added `stamp_shaders.metal` for Xcode Metal compilation

- **File**: `DrawingMobile/project-jit-sim.yml`
  - Added header search path for `jank-resources/src/jank`

- **File**: `DrawingMobile/project-jit-device.yml`
  - Same header search path addition

### 5. Integrated with drawing_canvas.hpp
- **File**: `src/vybe/app/drawing/native/drawing_canvas.hpp`
- Added `#include "metal_renderer.h"` for Apple platforms
- Added `use_metal_renderer` flag to `CanvasState`
- Added wrapper functions: `init_metal_renderer()`, `is_using_metal()`, `metal_*` functions
- Stub implementations when `HAVE_METAL_RENDERER` is 0

## Build Output

The build succeeded with Metal linking:
```
MetalLink .../default.metallib
  /usr/bin/metal -target air64-apple-ios17.0-simulator ... stamp_shaders.air
** BUILD SUCCEEDED **
```

## Key Lessons Learned

### 1. Objective-C at Global Scope
Objective-C `@interface` and `@implementation` declarations MUST be at global scope, not inside C++ namespaces. The fix was to move the ObjC code before the `namespace metal_stamp {` block.

### 2. Metal on iOS Simulator
- Metal is fully supported on iOS Simulator (arm64)
- Shaders compiled with `-target air64-apple-ios17.0-simulator`
- Can compile shaders from source at runtime as fallback

### 3. XcodeGen Configuration
- `.metal` files are automatically compiled by Xcode
- `.mm` files need explicit `-x objective-c++ -fobjc-arc` compiler flags
- Header search paths need to include jank-resources/src/jank for native headers

### 4. SDL3 + Metal Integration
- SDL3 uses CAMetalLayer internally on iOS/macOS
- Can extract Metal layer via `SDL_GetWindowProperties()` and `SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER`
- Need to check `TARGET_OS_IPHONE` vs macOS for different property keys

## Next Steps

1. **Test Metal Renderer**: Run the app and verify initialization
2. **Wire up input events**: Connect touch events to Metal stroke functions
3. **Add canvas compositing**: Render Metal canvas texture via SDL
4. **Profile performance**: Compare GPU stamps vs CPU tessellation

## Commands

```bash
# Sync and build
make drawing-ios-jit-sim-build

# Run on simulator
make drawing-ios-jit-sim-run

# Check Metal shader compilation
grep -i metal /tmp/drawing_ios_build.txt
```

## Files Modified/Created

- `src/vybe/app/drawing/native/stamp_shaders.metal` (NEW)
- `src/vybe/app/drawing/native/metal_renderer.h` (NEW)
- `src/vybe/app/drawing/native/metal_renderer.mm` (NEW)
- `src/vybe/app/drawing/native/drawing_canvas.hpp` (MODIFIED)
- `DrawingMobile/config-common.yml` (MODIFIED)
- `DrawingMobile/project-jit-sim.yml` (MODIFIED)
- `DrawingMobile/project-jit-device.yml` (MODIFIED)
