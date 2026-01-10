# Frame Animation Performance Research

## Current Problem
When rotating the Looom-style frame wheel, there's a visible hiccup when switching between frames. This is caused by synchronous CPU→GPU texture upload via `metal_stamp_restore_snapshot` which calls `replaceRegion` on every frame switch.

## How Professional Animation Apps Handle This

### Looom (by IORAMA)
- **Uses SVG/vectors** - Resolution-independent vector engine
- Frame switching is instant because there's no raster data to transfer
- Each layer (called "threads") is represented by rotating reels
- [App Store](https://apps.apple.com/us/app/looom/id1454153126)

### Procreate Dreams
- **Built on GPU-accelerated engine** - Uses GPU resources directly, not CPU
- This allows "very fast and smooth" rendering
- [Creative Bloq](https://www.creativebloq.com/art/animation/procreate-dreams-2-makes-the-ipad-animation-app-even-more-powerful-and-versatile)

### Callipeg
- Memory formula: `(storage_bytes / (width × height × 4)) / 3 = sheets`
- Stores 4 bytes per pixel (RGBA)
- Has cache issues that require app restart
- Advises working on small shots to avoid memory overflow
- [Callipeg FAQ](https://callipeg.com/faq/)

### RoughAnimator
- **Lower resolution preview** during scrubbing/playback
- Can decrease "Resolution for inactive layers and playback" to reduce lag
- Forward/back buttons step one frame at a time (frame scrubbing)
- [RoughAnimator User Guide](https://www.roughanimator.com/userguide-tablet/)

### FlipaClip
- **Smaller canvas = smoother playback**
- Hides/disables layers during playback to reduce load
- Audio sync prioritized over frame smoothness (frames dropped to match audio)
- Known glitches above 20fps
- [FlipaClip Knowledge Base](https://support.flipaclip.com/article/85-playback-preview-slow-or-skipping-frames)

## Technical Solutions for iOS/Metal

### Solution 1: Pre-cached GPU Textures (Recommended)
Store each frame as a separate Metal texture in GPU memory. Frame switching becomes just changing which texture to render - **zero CPU→GPU transfer**.

```objc
// Pre-allocate texture pool
NSMutableArray<id<MTLTexture>> *frameTextures; // 12 textures for 12 frames

// When saving a frame:
[frameTextures[frameIndex] replaceRegion:... withBytes:pixels...];

// When switching frames (INSTANT):
self.activeCanvasTexture = frameTextures[newFrameIndex];
```

**Pros:**
- Frame switching is instant (just pointer swap)
- No CPU→GPU transfer during rotation

**Cons:**
- Uses more GPU memory (12 × frame size)
- Need to keep textures in sync when editing

Sources:
- [Apple Metal Performance Optimization](https://developer.apple.com/videos/play/wwdc2015/610/)
- [Optimizing Texture Data](https://developer.apple.com/documentation/metal/textures/optimizing_texture_data)

### Solution 2: CVMetalTextureCache + IOSurface (Zero-Copy)
Use IOSurface for hardware-accelerated image buffer with GPU tracking.

```objc
// Create texture from IOSurface - LIVE binding, no copy!
CVMetalTextureCacheCreateTextureFromImage(cache, NULL, pixelBuffer, NULL,
    MTLPixelFormatBGRA8Unorm, width, height, 0, &textureRef);
```

**Key Insight:** IOSurface creates a "live connection" - any modification to the surface immediately reflects in the texture. Multiple textures can share the same GPU memory.

Sources:
- [CVMetalTextureCache Documentation](https://developer.apple.com/documentation/corevideo/cvmetaltexturecache-q3j)
- [Metal Camera Tutorial](https://navoshta.com/metal-camera-part-2-metal-texture/)

### Solution 3: Texture Atlas
Pack all 12 frames into one large texture (e.g., 4×3 grid). Frame switching = changing UV coordinates.

```objc
// All frames in one 4096×4096 texture
// Frame 0: UV (0,0) to (0.25, 0.33)
// Frame 1: UV (0.25,0) to (0.5, 0.33)
// etc.
```

**Pros:**
- Single texture binding
- Very fast switching (just UV change)

**Cons:**
- Limited by max texture size (4096×4096 or 16384×16384)
- All frames must be same resolution

Sources:
- [SpriteKit Texture Atlases](https://www.kodeco.com/144-spritekit-animations-and-texture-atlases-in-swift)
- [TexturePacker](https://www.codeandweb.com/texturepacker)

### Solution 4: Triple Buffering
Use 3 buffers: CPU writes to one while GPU reads from another.

```objc
// Frame N: GPU renders texture[0]
// Frame N+1: CPU updates texture[1], GPU renders texture[0]
// Frame N+2: CPU updates texture[2], GPU renders texture[1]
// etc.
```

Sources:
- [Metal Best Practices Guide](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/ResourceOptions.html)

### Solution 5: Shared Storage Mode (For Dynamic Textures)
Use `MTLStorageModeShared` for textures modified frequently by CPU and read by GPU.

```objc
MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:...];
desc.storageMode = MTLStorageModeShared; // Efficient for CPU+GPU access
```

Sources:
- [Metal Resource Options](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/ResourceOptions.html)

### Solution 6: Lower Resolution Preview
During rapid scrubbing, show lower-resolution version. Load full resolution on release.

**This is what RoughAnimator does** - separate resolution settings for active layer vs preview.

## Recommended Implementation for DrawingMobile

### Phase 1: Quick Win
1. ~~Remove debug logging~~ (Done)
2. Use `MTLStorageModeShared` for canvas texture if not already

### Phase 2: GPU Texture Pool (Best Solution)
1. Add `id<MTLTexture> frameTextures[12]` array in Metal renderer
2. When saving frame: copy canvas to `frameTextures[index]`
3. When switching: `canvasTexture = frameTextures[newIndex]`
4. No `replaceRegion` call during frame switching!

### Memory Estimate
- 12 frames × 2732×2048 (iPad Pro) × 4 bytes = ~267 MB
- Acceptable for modern iPads with 4-16GB RAM

### API Changes Needed
```cpp
// In metal_renderer.h
void metal_stamp_cache_frame_to_gpu(int frameIndex);  // Save current canvas to GPU
void metal_stamp_switch_to_cached_frame(int frameIndex);  // Instant switch
bool metal_stamp_is_frame_cached(int frameIndex);
```

## Summary

The hiccup is NOT from logging - it's from **synchronous CPU→GPU texture upload** (`replaceRegion`). The fix is to **pre-cache frames as GPU textures** so switching is just a texture pointer swap.

Professional apps either:
1. Use vectors (Looom) - no raster data
2. Pre-cache on GPU (Procreate Dreams)
3. Lower preview resolution (RoughAnimator)
4. Limit canvas size (FlipaClip, Callipeg)

For our use case (12 frames, raster-based), **GPU texture pooling** is the best solution.
