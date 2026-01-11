# Efficient Per-Stroke Undo System Plan

## Goal
Enable undo for **every single stroke** while keeping memory usage under **200 MB** (currently 1.66 GB).

## CRITICAL BUGS FOUND (Must Fix!)

### Bug 1: Snapshot Interval Says 1, Comment Says 10
**File**: `src/vybe/app/drawing/native/metal_renderer.mm:2299`
```cpp
g_undo_tree->setSnapshotInterval(1); // Snapshot every 10 strokes  <-- WRONG!
```
The comment says 10 but code says 1! This causes 33MB snapshot per stroke.

### Bug 2: Stroke Replay Missing begin_stroke/end_stroke
**File**: `src/vybe/app/drawing/native/metal_renderer.mm:2380-2386`
```cpp
// CURRENT (BROKEN):
for (const auto& pt : stroke.points) {
    metal_stamp::g_metal_renderer->add_stroke_point(pt.x, pt.y, pt.pressure);
}
metal_stamp::g_metal_renderer->render_current_stroke();

// SHOULD BE:
const auto& first = stroke.points[0];
g_metal_renderer->begin_stroke(first.x, first.y, first.pressure);
for (size_t i = 1; i < stroke.points.size(); i++) {
    g_metal_renderer->add_stroke_point(stroke.points[i].x, stroke.points[i].y, stroke.points[i].pressure);
}
g_metal_renderer->end_stroke();
```

### Bug 3: Clear Uses Wrong Background Color
**File**: `src/vybe/app/drawing/native/metal_renderer.mm:2343`
```cpp
g_metal_renderer->clear_canvas(1.0f, 1.0f, 1.0f, 1.0f);  // Pure white
// Should be: (0.95f, 0.95f, 0.92f, 1.0f)  // Paper off-white
```

## Architecture Analysis: IT'S ALREADY CORRECT!

The undo system uses **Command Pattern with Periodic Checkpoints** - the CORRECT approach:

1. **recordStroke()**: Stores stroke data (points + brush settings) ~20KB each
2. **restoreToNode()**: Finds nearest checkpoint, replays strokes from there
3. **Checkpoints**: Full canvas snapshots stored every N strokes

The ONLY problem was `setSnapshotInterval(1)` causing per-stroke snapshots.

## CRITICAL: Per-Frame Undo System

### Current Problem
- ONE global `g_undo_tree` for ALL 12 frames
- Drawing on frame 1, then frame 2, mixes undo history
- Undo on frame 2 might undo a stroke from frame 1!

### Solution: Vector of Undo Trees (IMPLEMENTED!)
```cpp
// BEFORE (wrong):
static std::unique_ptr<undo_tree::UndoTree> g_undo_tree = nullptr;

// AFTER (correct - uses vector for configurable frame count):
static std::vector<std::unique_ptr<undo_tree::UndoTree>> g_undo_trees;
static int g_current_undo_frame = 0;
static bool g_undo_initialized = false;

// Helper to get current frame's undo tree
static undo_tree::UndoTree* get_current_undo_tree() {
    if (!g_undo_initialized) return nullptr;
    if (g_current_undo_frame < 0 || g_current_undo_frame >= (int)g_undo_trees.size()) return nullptr;
    return g_undo_trees[g_current_undo_frame].get();
}
```

### New API Functions (IMPLEMENTED!)
```cpp
// Initialize with configurable frame count (for future multi-animation support)
void metal_stamp_undo_init_with_frames(int num_frames);  // Configurable!
void metal_stamp_undo_init();  // Default: 12 frames

// Call this when frame changes (from frame_next, frame_prev, frame_goto)
void metal_stamp_undo_set_frame(int frame);

// Query current state
int metal_stamp_undo_get_frame();       // Get current frame index
int metal_stamp_undo_get_frame_count(); // Get total frame count
```

### Integration with FrameStore
```cpp
// In drawing_mobile_ios.mm - framestore_load_frame():
static void framestore_load_frame(int index) {
    // Switch undo tree to match the frame we're loading
    metal_stamp_undo_set_frame(index);
    // ... rest of frame loading code
}

// In initialization:
metal_stamp_undo_init_with_frames(FrameStore::MAX_FRAMES);
```

### Memory Impact
- 12 frames × (50 strokes × 20KB + 5 checkpoints × 33MB)
- Worst case: 12 × 166MB = 2GB (if ALL frames have 50 strokes each)
- Realistic: Most frames empty or few strokes = ~200-400MB total

## Current State Analysis

### What We Have (Already Implemented!)
The codebase already has delta snapshot infrastructure that's **NOT BEING USED**:

```cpp
// In metal_renderer.h - ALREADY EXISTS
std::vector<uint8_t> capture_delta_snapshot(int x, int y, int w, int h);
bool restore_delta_snapshot(const std::vector<uint8_t>& pixels, int x, int y, int w, int h);

// In undo_tree.hpp - ALREADY EXISTS
struct CanvasSnapshot {
    bool isDelta = false;         // NOT USED!
    int deltaX = 0, deltaY = 0;   // NOT USED!
    // ...
};

// In metal_renderer.mm - ALREADY TRACKED
static float g_stroke_min_x, g_stroke_min_y;  // Bounding box - NOT USED FOR SNAPSHOTS!
static float g_stroke_max_x, g_stroke_max_y;
```

### Current Problem
In `metal_stamp_undo_init()` (line 2302), the snapshot callback captures the **FULL canvas**:
```cpp
g_undo_tree->setSnapshotCallback([]() -> std::shared_ptr<undo_tree::CanvasSnapshot> {
    // Captures ENTIRE 3840x2160 canvas = 33 MB every stroke!
    auto pixels = g_metal_renderer->capture_canvas_snapshot();
    // ...
});
```

## Research Findings

### How Professional Apps Handle This

| App | Approach | Memory Strategy |
|-----|----------|-----------------|
| **Procreate** | Tile-based + snapshots every N strokes | 250 undo levels, memory-capped |
| **GIMP** | Stores only altered tiles + metadata | "Only altered tiles and meta info stored" |
| **Krita** | 256x256 tile hash tables, shallow copies | Shares unchanged tiles between undo states |
| **Photoshop** | History states + History Brush | Fixed max limit (default 20) |

### Key Techniques from Research

1. **Command Pattern + Replay** ([Source](https://github.com/Harley-xk/MaLiang))
   - Store stroke data, replay to reconstruct
   - MaLiang iOS library uses this approach

2. **Dirty Rectangle / Bounding Box** ([Source](https://www.abidibo.net/blog/2011/10/12/development-undo-and-redo-functionality-canvas/))
   - Only store the changed rectangular region
   - "Since storing continuous states is too expensive, use discrete states"

3. **JotUI Approach** ([Source](https://adamwulf.me/2016/10/introducing-jotui-open-source-opengl-drawing-view-ios/))
   - "Calculate bounding rect of stroke, clip rendering to that box"
   - "Doing this in OpenGL was significantly faster"

4. **LZ4 Compression** ([Source](https://lz4.org/))
   - 500 MB/s compression speed
   - Multi-GB/s decompression
   - "Automatically implements RLE as special case" - great for drawings with empty areas

5. **GIMP's Tile System** ([Source](https://www.gimpusers.com/forums/gimp-developer/4174-how-does-undo-currently-work))
   - "Only altered tiles and necessary meta information stored"
   - Image resize just stores: `{ int width; int height; }`

## Proposed Solution: Hybrid Command + Delta Approach

### Strategy Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    UNDO NODE STRUCTURE                       │
├─────────────────────────────────────────────────────────────┤
│  Every Stroke Node:                                          │
│    ├── StrokeData (points, brush settings) ─── ~5-50 KB     │
│    ├── beforeSnapshot (delta, LZ4 compressed) ─ ~50-500 KB  │
│    └── boundingBox (x, y, w, h) ─────────────── 16 bytes    │
│                                                              │
│  Every 10th Stroke Node (checkpoint):                        │
│    └── fullSnapshot (LZ4 compressed) ────────── ~2-8 MB     │
└─────────────────────────────────────────────────────────────┘
```

### Memory Calculation (250 strokes)

| Component | Per Node | 250 Nodes | Notes |
|-----------|----------|-----------|-------|
| Stroke data | 20 KB avg | 5 MB | Points + brush settings |
| Delta snapshots | 200 KB avg | 50 MB | Bounding box only, LZ4 |
| Full checkpoints | 4 MB | 100 MB | Every 25 strokes = 10 checkpoints |
| **Total** | | **~155 MB** | **vs 1.66 GB currently** |

### Why This Works

1. **Typical stroke covers <5% of canvas**
   - Full canvas: 3840×2160 = 8.3M pixels × 4 = 33 MB
   - Typical stroke bbox: 500×500 = 250K pixels × 4 = 1 MB
   - With LZ4 (~4:1 for drawings): **~250 KB**

2. **LZ4 compression is nearly free**
   - Compress: 500 MB/s = 33 MB in 66ms
   - Decompress: 2+ GB/s = 33 MB in ~16ms
   - Drawings compress well (lots of uniform areas)

3. **Replay is fast for small gaps**
   - GPU renders 1000+ strokes/second
   - Replay 10 strokes: <10ms
   - Only replay between checkpoints

## Implementation Plan

### Phase 1: Use Existing Delta Infrastructure (2-3 hours)

The code already tracks bounding boxes and has delta capture functions. We just need to USE them.

**File: `src/vybe/app/drawing/native/metal_renderer.mm`**

#### Step 1.1: Capture pre-stroke delta before drawing

```cpp
// In undo_begin_stroke(), BEFORE drawing:
void undo_begin_stroke(float x, float y, float pressure) {
    // ALWAYS draw
    if (g_metal_renderer) {
        g_metal_renderer->begin_stroke(x, y, pressure);
    }

    if (!g_undo_tree) return;

    // NEW: Capture the BEFORE state of the bounding box region
    // We'll finalize the bbox when stroke ends
    g_stroke_needs_before_snapshot = true;  // New flag

    // ... rest of existing code
}
```

#### Step 1.2: Finalize delta on stroke end

```cpp
// In undo_end_stroke():
void undo_end_stroke() {
    if (g_metal_renderer) {
        g_metal_renderer->end_stroke();
    }

    if (!g_is_recording_stroke || !g_undo_tree) {
        g_is_recording_stroke = false;
        return;
    }

    // NEW: Capture delta snapshot of the stroke's bounding box
    if (g_stroke_bbox_valid) {
        // Expand bbox slightly for anti-aliasing
        int x = std::max(0, (int)g_stroke_min_x - 2);
        int y = std::max(0, (int)g_stroke_min_y - 2);
        int x2 = std::min(canvasWidth, (int)g_stroke_max_x + 2);
        int y2 = std::min(canvasHeight, (int)g_stroke_max_y + 2);
        int w = x2 - x;
        int h = y2 - y;

        // Capture BEFORE state (from GPU cache or reconstruct)
        // Store with stroke for undo
        auto beforePixels = /* captured at stroke start */;

        // Create delta snapshot
        auto snapshot = std::make_shared<undo_tree::CanvasSnapshot>();
        snapshot->isDelta = true;
        snapshot->deltaX = x;
        snapshot->deltaY = y;
        snapshot->width = w;
        snapshot->height = h;
        snapshot->pixels = std::move(beforePixels);

        // Attach to current stroke node
        g_current_stroke.beforeSnapshot = snapshot;
    }

    g_undo_tree->recordStroke(g_current_stroke);
    // ...
}
```

#### Step 1.3: Modify undo to use delta restore

```cpp
// In restoreToNode() or the restore callback:
g_undo_tree->setRestoreCallback([](const undo_tree::CanvasSnapshot& snapshot) {
    if (snapshot.isDelta) {
        // Restore just the delta region!
        g_metal_renderer->restore_delta_snapshot(
            snapshot.pixels,
            snapshot.deltaX, snapshot.deltaY,
            snapshot.width, snapshot.height
        );
    } else {
        // Full snapshot restore
        g_metal_renderer->restore_canvas_snapshot(snapshot.pixels,
            snapshot.canvasWidth, snapshot.canvasHeight);
    }
});
```

### Phase 2: Add LZ4 Compression (1-2 hours)

**Add LZ4 to the build** (header-only option available):
```cpp
#include <lz4.h>  // Single header, BSD license

std::vector<uint8_t> compressPixels(const std::vector<uint8_t>& pixels) {
    int maxSize = LZ4_compressBound(pixels.size());
    std::vector<uint8_t> compressed(maxSize);
    int actualSize = LZ4_compress_default(
        (const char*)pixels.data(),
        (char*)compressed.data(),
        pixels.size(),
        maxSize
    );
    compressed.resize(actualSize);
    return compressed;
}

std::vector<uint8_t> decompressPixels(const std::vector<uint8_t>& compressed, int originalSize) {
    std::vector<uint8_t> decompressed(originalSize);
    LZ4_decompress_safe(
        (const char*)compressed.data(),
        (char*)decompressed.data(),
        compressed.size(),
        originalSize
    );
    return decompressed;
}
```

### Phase 3: Optimize Checkpoint Strategy (1 hour)

```cpp
// In recordStroke():
void UndoTree::recordStroke(const StrokeData& stroke) {
    // ... create node ...

    // Store full checkpoint every N strokes (not every stroke!)
    bool needsCheckpoint = (current_->depth() % 25 == 0);

    if (needsCheckpoint && onSnapshot_) {
        // Full compressed snapshot for fast random access
        current_->snapshot = onSnapshot_();  // Already uses LZ4
        std::cout << "[UndoTree] Checkpoint at depth " << current_->depth() << std::endl;
    }

    // Delta snapshot is stored with stroke data (captured in undo_end_stroke)
}
```

### Phase 4: GPU-Based Delta Capture (Optional, Advanced)

Instead of CPU readback, use GPU blit to capture delta:

```objc
// In MetalStampRendererImpl
- (std::vector<uint8_t>)captureRegion:(MTLRegion)region {
    // Use blit encoder to copy region to staging buffer
    // Much faster than full readback
}
```

## Alternative Approaches Considered

### Approach A: Pure Command Pattern (No Snapshots)
- **Pro**: Minimal memory (just stroke data)
- **Con**: Undo requires replaying ALL strokes from root
- **Con**: 250 strokes × 10ms each = 2.5 seconds for deep undo
- **Verdict**: Too slow for interactive use

### Approach B: Tile-Based (Like Krita/GIMP)
- **Pro**: Industry standard, very efficient
- **Con**: Major architectural change
- **Con**: Requires rewriting canvas representation
- **Verdict**: Good for v2.0, too invasive now

### Approach C: GPU Texture Diffing
- **Pro**: No CPU readback needed
- **Con**: Requires compute shaders
- **Con**: Complex to implement correctly
- **Verdict**: Interesting future optimization

### Approach D: Hybrid Delta + Checkpoints (RECOMMENDED)
- **Pro**: Uses existing infrastructure
- **Pro**: 10-20x memory reduction
- **Pro**: Fast undo (<16ms typical)
- **Pro**: Can be implemented incrementally
- **Verdict**: Best balance of effort vs reward

## Testing Plan

1. **Memory Monitoring**
   ```objc
   #import <mach/mach.h>

   size_t getMemoryUsage() {
       struct task_basic_info info;
       mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
       task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &size);
       return info.resident_size;
   }
   ```

2. **Benchmark Scenarios**
   - Draw 50 strokes, measure memory
   - Draw 250 strokes, measure memory
   - Undo 50 times, measure time
   - Redo 50 times, measure time

3. **Edge Cases**
   - Very large strokes (screen-filling)
   - Very small strokes (single tap)
   - Rapid undo/redo sequences
   - Memory pressure simulation

## Files to Modify

| File | Changes |
|------|---------|
| `src/vybe/app/drawing/native/metal_renderer.mm` | Delta capture in stroke functions |
| `src/vybe/app/drawing/native/undo_tree.hpp` | Add beforeSnapshot to StrokeData |
| `src/vybe/app/drawing/native/undo_tree.cpp` | Use delta in restoreToNode |
| `DrawingMobile/drawing_mobile_ios.mm` | Re-enable undo init |
| `Makefile` | Add LZ4 dependency |

## Expected Results

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Memory (250 strokes) | 1.66 GB | ~150 MB | **11x reduction** |
| Undo latency | ~500ms | <20ms | **25x faster** |
| Redo latency | ~500ms | <20ms | **25x faster** |
| Checkpoint size | 33 MB | ~4 MB | **8x smaller** |

## References

- [JotUI: OpenGL Drawing for iOS](https://adamwulf.me/2016/10/introducing-jotui-open-source-opengl-drawing-view-ios/)
- [MaLiang: Metal Drawing Library](https://github.com/Harley-xk/MaLiang)
- [GIMP Undo Architecture](https://www.gimpusers.com/forums/gimp-developer/4174-how-does-undo-currently-work)
- [Krita Performance Settings](https://docs.krita.org/en/reference_manual/preferences/performance_settings.html)
- [LZ4 Compression](https://lz4.org/)
- [Dirty Rectangle Rendering](https://apachecon.com/acasia2021/sessions/1087.html)
- [Canvas Undo Development](https://www.abidibo.net/blog/2011/10/12/development-undo-and-redo-functionality-canvas/)
