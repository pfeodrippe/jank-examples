# Looom-Style Thread Animation System - ULTRATHINK Investigation

## Date: 2026-01-09

## Executive Summary

This document investigates how to implement Looom's "thread" animation system - where each layer is an independent animated timeline - for our iOS drawing app. Looom's approach is fundamentally different from traditional layer systems: each thread has its own frame count, frame rate, and playback mode, all playing simultaneously.

---

## 1. What is a Looom Thread?

### Core Concept

From [Looom's User Guide](https://www.iorama.studio/looom-user-guide):

> "In Looom each layer is its own timeline which is independent in its length and speed, these layers are called threads and are represented by rotating reels."

A **thread** is NOT just a layer - it's an **independent animation timeline** with:
- Its own set of frames (drawings)
- Its own frame rate (FPS)
- Its own play mode (forward, backward, ping-pong, random)
- Its own visual properties (color, stroke style, opacity, blend mode)

### Weave = Composition

A **weave** is the combined output of all threads playing together. The weave has:
- Global playback control
- Background color/image
- Up to 5 threads
- Speed multiplier affecting all threads

### Visual Metaphor

Threads are represented as **rotating reels** (like film reels or music boxes):
- Each frame is a dot on the reel
- The reel rotates as the animation plays
- Different threads rotate at different speeds
- The combination creates complex polyrhythmic animations

---

## 2. Thread Properties (Full Specification)

Based on research from [Looom User Guide](https://www.iorama.studio/looom-user-guide) and [Apple Developer Article](https://developer.apple.com/news/?id=s26ze13m):

### Per-Thread Properties

| Property | Description | Type | Default |
|----------|-------------|------|---------|
| `frames` | Array of drawings | Drawing[] | [] |
| `fps` | Frames per second | float | 12.0 |
| `playMode` | Forward/Backward/PingPong/Random | enum | Forward |
| `currentFrame` | Current frame index | int | 0 |
| `color` | Stroke/fill color | RGB | varies |
| `strokeStyle` | Line or Fill | enum | Line |
| `lineWidth` | Stroke width (line mode) | float | 3.0 |
| `opacity` | Layer opacity | float | 1.0 |
| `blendMode` | Alpha/Additive/Multiply/Invert | enum | Alpha |
| `clipToBelow` | Mask to thread below | bool | false |
| `pressureSensitive` | Enable pressure | bool | true |
| `visible` | Show/hide thread | bool | true |
| `locked` | Prevent editing | bool | false |

### Per-Frame Properties

| Property | Description | Type |
|----------|-------------|------|
| `strokes` | Array of stroke data | Stroke[] |
| `bookmark` | Reference frame marker | bool |
| `duration` | Hold frames (optional) | int |

### Global Weave Properties

| Property | Description |
|----------|-------------|
| `threads` | Array of 1-5 threads |
| `backgroundColor` | Canvas background |
| `globalSpeed` | Speed multiplier |
| `gridVisible` | Show alignment grid |
| `onionSkin` | Onion skin settings |

---

## 3. Playback Modes Deep Dive

### Forward Mode
```
Frame sequence: 0 → 1 → 2 → 3 → 0 → 1 → 2 → 3 → ...
```

### Backward Mode
```
Frame sequence: 3 → 2 → 1 → 0 → 3 → 2 → 1 → 0 → ...
```

### Ping-Pong Mode
```
Frame sequence: 0 → 1 → 2 → 3 → 2 → 1 → 0 → 1 → 2 → ...

Special: Can anchor to bookmarked frames to change reversal point
```

### Random Mode
```
Frame sequence: 2 → 0 → 3 → 1 → 0 → 2 → ... (random each cycle)
```

### Independent Timing Example

Thread A: 4 frames at 12 FPS = 0.33s per loop
Thread B: 6 frames at 8 FPS = 0.75s per loop

Result: Complex polyrhythmic animation where patterns align every LCM of loop lengths.

---

## 4. Current Architecture Analysis

### What We Have

From `metal_renderer.h`:

```cpp
// Current: Single canvas with stroke-based drawing
class MetalStampRenderer {
    // One canvas texture
    void clear_canvas(float r, float g, float b, float a);

    // Stroke operations on single canvas
    void begin_stroke(float x, float y, float pressure);
    void add_stroke_point(float x, float y, float pressure);
    void end_stroke();

    // Snapshots for undo (full canvas)
    std::vector<uint8_t> capture_canvas_snapshot();
    bool restore_canvas_snapshot(const std::vector<uint8_t>& pixels, int width, int height);
};
```

### What We Need

```cpp
// Thread-based multi-timeline animation system
class AnimationThread {
    std::vector<Frame> frames;
    float fps;
    PlayMode playMode;
    int currentFrameIndex;
    ThreadProperties properties;

    void advanceFrame(float deltaTime);
    Frame* getCurrentFrame();
    void addFrame();
    void deleteFrame(int index);
};

class Weave {
    std::vector<AnimationThread> threads;  // Up to 5
    float globalSpeed;
    bool isPlaying;

    void update(float deltaTime);
    void render();
    void compositeThreads();
};
```

---

## 5. Implementation Architecture

### Option A: Canvas-Per-Frame (Memory Heavy)

Each frame stores a full canvas snapshot.

```cpp
struct Frame {
    std::vector<uint8_t> canvasPixels;  // Full RGBA snapshot
    int width, height;
    bool hasContent;
};

struct Thread {
    std::vector<Frame> frames;
    // ...
};
```

**Pros:**
- Simple to implement
- Fast frame switching (just copy texture)
- Easy undo per frame

**Cons:**
- Memory intensive: 1024x1024 RGBA = 4MB per frame
- 5 threads × 24 frames = 480MB
- Not scalable for longer animations

### Option B: Stroke-Per-Frame (Recommended)

Each frame stores stroke data, re-rendered on demand.

```cpp
struct StrokePoint {
    float x, y;
    float pressure;
    float timestamp;  // For velocity calculation
};

struct Stroke {
    std::vector<StrokePoint> points;
    BrushSettings brush;
    uint32_t color;
};

struct Frame {
    std::vector<Stroke> strokes;
    bool needsRender;  // Dirty flag for caching

    // Optional: Cached render for playback
    id<MTLTexture> cachedTexture;  // Render on first view, invalidate on edit
};
```

**Pros:**
- Memory efficient: Strokes are tiny (~100 bytes per stroke)
- Resolution independent (can re-render at any size)
- Perfect for SVG export (matches Looom's format)
- Enables non-destructive editing

**Cons:**
- More complex rendering during playback
- Need stroke caching for smooth playback
- More complex undo (per-stroke vs per-canvas)

### Option C: Hybrid Approach (Best of Both)

Store strokes for editing, cache rendered frames for playback.

```cpp
struct Frame {
    // Source data (always preserved)
    std::vector<Stroke> strokes;

    // Render cache (generated on demand)
    id<MTLTexture> renderCache;
    bool cacheValid;

    void invalidateCache() { cacheValid = false; }

    id<MTLTexture> getRenderedTexture() {
        if (!cacheValid) {
            renderCache = renderStrokes(strokes);
            cacheValid = true;
        }
        return renderCache;
    }
};
```

---

## 6. Proposed Data Model

### Core Structures

```cpp
// Forward declarations
struct Thread;
struct Frame;
struct Stroke;
struct Weave;

// Play modes
enum class PlayMode : int {
    Forward = 0,
    Backward = 1,
    PingPong = 2,
    Random = 3
};

// Blend modes
enum class BlendMode : int {
    Alpha = 0,      // Normal
    Additive = 1,   // Screen-like
    Multiply = 2,   // Darken
    Invert = 3      // Difference
};

// Thread visual properties
struct ThreadStyle {
    float r, g, b, a;           // Color
    float lineWidth;            // Stroke width
    float opacity;              // Layer opacity
    BlendMode blendMode;
    bool isFillMode;            // true = fill, false = line
    bool pressureSensitive;
    bool clipToBelow;           // Mask to thread below
};

// Single stroke within a frame
struct AnimStroke {
    std::vector<StrokePoint> points;
    BrushSettings brush;
    ThreadStyle style;          // Captures style at draw time
};

// Single frame within a thread
struct AnimFrame {
    std::vector<AnimStroke> strokes;
    bool isBookmark;            // Reference frame marker

    // Render cache
    id<MTLTexture> cache;
    bool cacheValid;
};

// Animation thread (independent timeline)
struct AnimThread {
    std::string name;
    std::vector<AnimFrame> frames;
    ThreadStyle style;

    // Playback
    float fps;
    PlayMode playMode;
    int currentFrameIndex;
    float frameAccumulator;     // For sub-frame timing
    int pingPongDirection;      // +1 or -1

    // State
    bool visible;
    bool locked;
    bool selected;              // Currently editing this thread
};

// Complete animation project
struct Weave {
    std::vector<AnimThread> threads;  // Max 5
    int activeThreadIndex;

    // Global settings
    float backgroundColor[4];
    float globalSpeed;
    bool isPlaying;

    // Onion skin
    int onionSkinBefore;        // Frames to show before
    int onionSkinAfter;         // Frames to show after
    float onionSkinOpacity;
};
```

### Timeline Update Logic

```cpp
void AnimThread::update(float deltaTime, float globalSpeed) {
    if (frames.empty()) return;

    float effectiveFps = fps * globalSpeed;
    float frameTime = 1.0f / effectiveFps;

    frameAccumulator += deltaTime;

    while (frameAccumulator >= frameTime) {
        frameAccumulator -= frameTime;
        advanceFrame();
    }
}

void AnimThread::advanceFrame() {
    switch (playMode) {
        case PlayMode::Forward:
            currentFrameIndex = (currentFrameIndex + 1) % frames.size();
            break;

        case PlayMode::Backward:
            currentFrameIndex = (currentFrameIndex - 1 + frames.size()) % frames.size();
            break;

        case PlayMode::PingPong:
            currentFrameIndex += pingPongDirection;
            if (currentFrameIndex >= frames.size() - 1) {
                currentFrameIndex = frames.size() - 1;
                pingPongDirection = -1;
            } else if (currentFrameIndex <= 0) {
                currentFrameIndex = 0;
                pingPongDirection = 1;
            }
            break;

        case PlayMode::Random:
            currentFrameIndex = rand() % frames.size();
            break;
    }
}
```

---

## 7. Rendering Pipeline

### Per-Frame Rendering

```cpp
void renderFrame(AnimFrame& frame, id<MTLTexture> target) {
    if (frame.cacheValid && frame.cache) {
        // Fast path: blit cached texture
        blitTexture(frame.cache, target);
        return;
    }

    // Slow path: re-render strokes
    clearTexture(target);
    for (const auto& stroke : frame.strokes) {
        renderStroke(stroke, target);
    }

    // Cache the result
    frame.cache = copyTexture(target);
    frame.cacheValid = true;
}
```

### Compositing Threads

```cpp
void Weave::render(id<MTLTexture> output) {
    // Clear to background
    clearTexture(output, backgroundColor);

    // Render threads bottom to top
    for (int i = threads.size() - 1; i >= 0; i--) {
        AnimThread& thread = threads[i];
        if (!thread.visible) continue;

        AnimFrame& frame = thread.frames[thread.currentFrameIndex];
        id<MTLTexture> frameTexture = frame.getRenderedTexture();

        // Composite with blend mode
        compositeTexture(frameTexture, output,
                        thread.style.opacity,
                        thread.style.blendMode,
                        thread.style.clipToBelow ? threads[i+1].getCurrentTexture() : nullptr);
    }

    // Render onion skins for active thread (if editing)
    if (!isPlaying && activeThreadIndex >= 0) {
        renderOnionSkins(threads[activeThreadIndex], output);
    }
}
```

### Onion Skin Rendering

```cpp
void renderOnionSkins(AnimThread& thread, id<MTLTexture> output) {
    int current = thread.currentFrameIndex;

    // Past frames (tinted blue/red)
    for (int i = 1; i <= onionSkinBefore; i++) {
        int idx = (current - i + thread.frames.size()) % thread.frames.size();
        float opacity = onionSkinOpacity * (1.0f - (float)i / (onionSkinBefore + 1));
        compositeTexture(thread.frames[idx].getRenderedTexture(), output,
                        opacity, BlendMode::Alpha, nullptr,
                        0.5f, 0.5f, 1.0f);  // Blue tint
    }

    // Future frames (tinted green)
    for (int i = 1; i <= onionSkinAfter; i++) {
        int idx = (current + i) % thread.frames.size();
        float opacity = onionSkinOpacity * (1.0f - (float)i / (onionSkinAfter + 1));
        compositeTexture(thread.frames[idx].getRenderedTexture(), output,
                        opacity, BlendMode::Alpha, nullptr,
                        0.5f, 1.0f, 0.5f);  // Green tint
    }
}
```

---

## 8. UI Design Considerations

### Thread Reel Visualization

Looom's genius is the **rotating reel metaphor**:

```
    Thread 1 (12 FPS, 4 frames)     Thread 2 (8 FPS, 6 frames)

         ●                               ●
       ●   ●                           ●   ●
         ●                            ●     ●
                                       ●   ●
                                         ●

    Dots = frames                    More dots = more frames
    Rotation = playback              Different speeds = different rotations
```

### Controls Needed

1. **Thread Selection**: Tap reel to select for editing
2. **Frame Navigation**:
   - Swipe on reel to scrub
   - Looom uses "Pulley" interface (drag down to advance)
3. **Add/Delete Frames**: Buttons or gestures
4. **Play/Pause**: Global and per-thread
5. **FPS Control**: Slider per thread
6. **Play Mode**: Cycle through modes
7. **Properties Panel**: Color, opacity, blend mode, etc.

---

## 9. File Format Considerations

### Option A: Custom Binary Format

```cpp
// Header
struct WeaveHeader {
    char magic[4];      // "WEVE"
    uint32_t version;
    uint32_t threadCount;
    uint32_t flags;
};

// Compact, fast to load, but not human-readable
```

### Option B: JSON + Binary Blobs

```json
{
  "version": 1,
  "threads": [
    {
      "name": "Thread 1",
      "fps": 12,
      "playMode": "forward",
      "style": { "color": "#FF0000", "lineWidth": 3.0 },
      "frames": [
        { "strokesFile": "thread0_frame0.strokes", "bookmark": false }
      ]
    }
  ],
  "globalSpeed": 1.0,
  "backgroundColor": "#FFFFFF"
}
```

### Option C: SVG (Looom Compatible)

Looom uses SVG as native format. From [GitHub looom-tools](https://github.com/mattdesl/looom-tools):

```xml
<svg>
  <g id="thread-0" data-fps="12" data-playmode="forward">
    <g id="frame-0" opacity="1">
      <path d="M10,10 L100,100 L50,150" stroke="#FF0000" fill="none"/>
    </g>
    <g id="frame-1" opacity="0">
      <!-- Hidden when not current frame -->
    </g>
  </g>
</svg>
```

**Recommendation**: Use JSON + stroke data internally, export to SVG for compatibility.

---

## 10. Implementation Roadmap

### Phase 1: Basic Thread System (Core)

1. Create `AnimThread` and `AnimFrame` data structures
2. Implement frame storage (stroke-based)
3. Add frame switching (next/prev/goto)
4. Render single thread with frame caching
5. Add simple UI for frame navigation

### Phase 2: Multi-Thread Composition

1. Support multiple threads (up to 5)
2. Implement thread compositing
3. Add blend modes (alpha, additive, multiply)
4. Thread reordering (drag to change z-order)
5. Thread visibility toggle

### Phase 3: Independent Playback

1. Per-thread FPS control
2. Play modes (forward, backward, ping-pong, random)
3. Global play/pause with speed control
4. Frame bookmarks for ping-pong anchors
5. Polyrhythmic playback (threads at different speeds)

### Phase 4: Onion Skinning

1. Show adjacent frames while editing
2. Configurable before/after frame count
3. Tinting for past/future distinction
4. Bookmark-based reference frames

### Phase 5: Polish & Export

1. Thread reel visualization
2. Pulley-style frame navigation
3. SVG export for web compatibility
4. GIF/video export
5. Project save/load

---

## 11. Key Technical Challenges

### Challenge 1: Memory Management

**Problem**: Many frames × many threads = lots of textures

**Solution**:
- Use stroke-based storage (tiny)
- Lazy render caching (only cache visible frames)
- LRU eviction for frame caches
- Render on-demand during playback

### Challenge 2: Smooth Playback

**Problem**: Re-rendering strokes per frame is slow

**Solution**:
- Pre-render frames in background thread
- Double-buffer frame textures
- Prioritize rendering frames near current position
- Lower quality during fast scrubbing

### Challenge 3: Undo Across Threads

**Problem**: Undo needs to track which thread and frame was modified

**Solution**:
- Extend undo system to include thread/frame context
- Store: `{threadIndex, frameIndex, operation, strokeData}`
- Undo restores to correct thread/frame

### Challenge 4: Touch Interaction

**Problem**: Need to distinguish drawing vs UI gestures

**Solution** (Looom's approach):
- Apple Pencil = drawing only
- Finger = UI control (pulley, reel rotation)
- Two-finger = pan/zoom canvas

---

## 12. References

- [Looom User Guide](https://www.iorama.studio/looom-user-guide) - Official documentation
- [Apple Developer: Behind the Design](https://developer.apple.com/news/?id=s26ze13m) - Design philosophy
- [looom-tools GitHub](https://github.com/mattdesl/looom-tools) - SVG parsing/rendering tools

---

## 13. Summary

Implementing Looom-style threads requires:

1. **Data Model**: Threads contain frames, frames contain strokes
2. **Independent Timelines**: Each thread has own FPS and play mode
3. **Efficient Storage**: Stroke-based with render caching
4. **Compositing**: Blend modes, opacity, clipping masks
5. **UI**: Reel metaphor, pulley navigation, onion skins

The key insight from Looom is treating animation as **musical/rhythmic** rather than film-based. Threads are like instruments playing together, each at their own tempo, creating complex polyrhythmic visual compositions.

This is fundamentally different from traditional layer-based animation where all layers share the same timeline. The implementation should embrace this difference rather than trying to force it into a traditional model.
