# Looom-Style Thread Animation System Implementation

## What I Learned

### Looom Animation Concepts
- **Thread**: An independent animated layer with its own FPS, frames, and play mode
- **Frame**: Contains multiple strokes that are drawn and played back together
- **Weave**: A composition of multiple threads playing simultaneously (polyrhythmic)
- **Play Modes**: Forward, Backward, PingPong, Random - each thread can have independent timing

### Architecture Decisions
1. **Stroke-based storage** instead of canvas snapshots - much more memory efficient
2. **C++ data structures** for animation state - fast playback and frame navigation
3. **Dual recording** - strokes recorded to both Metal renderer (for display) and animation system (for playback)
4. **C API for JIT integration** - extern "C" functions allow jank to call animation functions directly

### Key Data Structures (animation_thread.h)
```cpp
struct StrokePoint { float x, y, pressure, timestamp; };
struct StrokeBrush { /* brush settings captured at draw time */ };
struct AnimStroke { vector<StrokePoint> points; StrokeBrush brush; float r,g,b,a; };
struct AnimFrame { vector<AnimStroke> strokes; bool isBookmark; int32_t cacheTextureId; };
struct AnimThread { vector<AnimFrame> frames; float fps; PlayMode playMode; int currentFrameIndex; };
struct Weave { vector<AnimThread> threads; int activeThreadIndex; float globalSpeed; bool isPlaying; };
```

### Integration Pattern
The animation system integrates with existing stroke handling:
1. `start-drawing!` calls both `metal_begin_stroke` AND `begin_anim_stroke`
2. `add-drawing-point!` calls both `metal_add_stroke_point` AND `add_anim_stroke_point`
3. `end-drawing!` calls both `metal_end_stroke` AND `end_anim_stroke`

This dual-recording means strokes are:
- Rendered immediately via Metal (visual feedback)
- Stored in animation frames for playback

## Commands I Ran

```bash
# Build iOS app with animation system
make drawing-ios-jit-sim-build

# Run in simulator
make drawing-ios-jit-sim-run

# Check build output for errors
grep -E "(error:|BUILD SUCCEEDED)" /tmp/ios_build.txt
```

## Files Created/Modified

### New Files
- `src/vybe/app/drawing/native/animation_thread.h` - Data structures + C API declarations
- `src/vybe/app/drawing/native/animation_thread.mm` - C API implementation

### Modified Files
- `DrawingMobile/config-common.yml` - Added animation_thread.mm to CommonSources template
- `src/vybe/app/drawing/native/drawing_canvas.hpp` - Added animation wrapper functions
- `src/vybe/app/drawing/state.jank` - Added animation stroke recording calls + frame navigation functions
- `src/vybe/app/drawing.jank` - Added animation system initialization

## What's Next

### Phase 2: Frame Rendering
- Implement `anim_render_frame()` to replay strokes from AnimFrame to Metal canvas
- Add frame caching (render once, cache as texture)
- Support onion skinning (show ghost frames before/after current)

### Phase 3: Playback
- Implement animation update loop that advances frames based on FPS
- Add play/pause/stop controls to UI
- Support independent thread timing (polyrhythmic animation)

### Phase 4: UI
- Add frame filmstrip/reel visualization (like Looom's spinning reels)
- Add thread management (add/delete/select threads)
- Add FPS and play mode controls per thread

### Phase 5: Persistence
- Serialize Weave to JSON/binary for save/load
- Export animation as GIF or video

## Key Insights

1. **Memory efficiency**: Storing strokes (~KB per frame) vs canvas snapshots (~MB per frame) makes multi-frame animation practical on mobile

2. **Decoupled playback**: Animation data is separate from Metal rendering, allowing frame preview without full re-render

3. **Looom's genius**: Independent FPS per thread creates polyrhythmic animations - a 3-frame thread at 12fps and a 5-frame thread at 8fps create complex, organic motion patterns

4. **C API pattern**: Using `extern "C"` for all animation functions ensures stable symbol names for JIT integration
