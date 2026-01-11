# iOS OOM (Out of Memory) Investigation

## Executive Summary

The iOS drawing app has **significant memory pressure** from multiple sources. Total memory usage can reach **2-3 GB** under normal use, which exceeds the available memory on many iOS devices (iPads typically have 3-8 GB total, with ~2-4 GB available to apps).

### Critical Memory Consumers (Ranked)

| Component | Size Per Unit | Count | Total | Priority |
|-----------|--------------|-------|-------|----------|
| **Undo Snapshots** | 33.1 MB | 5/tree × 12 trees | **1.99 GB** | 🔴 CRITICAL |
| GPU Frame Cache | 33.1 MB | 12 frames | 397 MB | 🟡 HIGH |
| CPU Frame Backup | 33.1 MB | 12 frames | 397 MB | 🟡 HIGH |
| Brush Textures | ~1-5 MB | ~50 brushes | ~100-200 MB | 🟢 MEDIUM |
| Stroke Data | ~250 KB | 50/tree × 12 | ~150 MB | 🟢 LOW |
| **TOTAL ESTIMATED** | | | **~3.1 GB** | 🔴 |

---

## Root Cause Analysis

### 1. Undo Tree Snapshots (THE PRIMARY CULPRIT)

**Location**: `src/vybe/app/drawing/native/undo_tree.cpp`, `metal_renderer.mm:2390-2425`

**Current Configuration**:
```cpp
tree->setMaxNodes(50);         // 50 undo levels per frame
tree->setSnapshotInterval(10); // Checkpoint every 10 strokes
```

**Problem**:
- Canvas size: 3840 × 2160 × 4 bytes = **33.1 MB per snapshot**
- Each undo tree stores a snapshot every 10 strokes
- Max nodes = 50, so max ~5 snapshots per tree
- 12 animation frames × 5 snapshots × 33.1 MB = **~1.99 GB**

**Why it's worse than expected**:
- Snapshots use `std::shared_ptr<CanvasSnapshot>` (undo_tree.hpp:107)
- Each snapshot contains `std::vector<uint8_t> pixels` (undo_tree.hpp:77)
- Trimming only removes nodes, not shared snapshots
- If user draws across multiple frames heavily, memory accumulates quickly

**Code Path**:
```
recordStroke() → if depth % snapshotInterval == 0 → onSnapshot_()
  → capture_canvas_snapshot() → 33.1 MB allocation
```

### 2. Frame Storage Duplication

**Location**: `DrawingMobile/drawing_mobile_ios.mm:1264-1322`

**Problem**: Double-storing frame data:

```cpp
struct FrameStore {
    std::vector<std::vector<uint8_t>> frames;  // CPU backup: 12 × 33.1 MB
    bool gpuCacheReady;  // GPU cache: 12 × 33.1 MB (separate)
};
```

**Memory Usage**:
- GPU Frame Cache: 12 × 33.1 MB = **397 MB** (Metal textures)
- CPU Frame Backup: 12 × 33.1 MB = **397 MB** (std::vector)
- Total: **794 MB** for same data stored twice

**Why both exist**:
- GPU cache: Fast frame switching (instant blit)
- CPU backup: Persistence (not currently implemented)

### 3. Brush Texture Accumulation

**Location**: `DrawingMobile/brush_importer.mm`

**Loaded brushsets** (from config-common.yml):
- `brushes.brushset`
- `BetterThanBasics.brushset`
- `WC_brushes_For_Paperlike.brushset`
- Additional extracted brushes

**Per-brush memory**:
- Shape texture: typically 512×512 to 2048×2048 (1-16 MB)
- Grain texture: typically 512×512 to 2048×2048 (1-16 MB)
- Thumbnail: 128×128 (~65 KB)

**Estimated total**: ~100-200 MB for all loaded brushes

### 4. No Memory Warning Handling

**Critical Missing Feature**: The app does NOT respond to iOS memory warnings.

```bash
# Searched for but NOT FOUND:
grep -r "didReceiveMemoryWarning\|memory_warning\|os_proc_available_memory" DrawingMobile/
# No results!
```

iOS sends memory warnings before killing apps. Without handling these, the app can't proactively reduce memory.

---

## Memory Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           TOTAL MEMORY USAGE                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ UNDO SYSTEM (12 trees × ~166 MB each = ~1.99 GB)                   │    │
│  │                                                                     │    │
│  │  Frame 0 UndoTree:    Frame 1 UndoTree:    ...   Frame 11:        │    │
│  │  ┌─────────────────┐  ┌─────────────────┐        ┌─────────────┐  │    │
│  │  │ Root            │  │ Root            │        │ Root        │  │    │
│  │  │ ├─Node1         │  │ ├─Node1         │        │ ├─Node1     │  │    │
│  │  │ │ (stroke data) │  │ │ (stroke data) │        │ │           │  │    │
│  │  │ ├─Node10 ★      │  │ ├─Node10 ★      │        │ ├─Node10 ★  │  │    │
│  │  │ │ SNAPSHOT 33MB │  │ │ SNAPSHOT 33MB │        │ │ SNAP 33MB │  │    │
│  │  │ ├─Node20 ★      │  │ ├─Node20 ★      │        │ ...         │  │    │
│  │  │ │ SNAPSHOT 33MB │  │ │ SNAPSHOT 33MB │        │             │  │    │
│  │  │ ...             │  │ ...             │        │             │  │    │
│  │  │ (max 5 snaps)   │  │ (max 5 snaps)   │        │             │  │    │
│  │  └─────────────────┘  └─────────────────┘        └─────────────┘  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ FRAME STORAGE (DUPLICATED!)                          ~794 MB       │    │
│  │                                                                     │    │
│  │  GPU Cache (MTLTexture[12]):  CPU Backup (vector<vector<uint8_t>>):│    │
│  │  ┌──────────────────────────┐ ┌──────────────────────────────────┐ │    │
│  │  │ Frame 0: 33 MB           │ │ Frame 0: 33 MB                   │ │    │
│  │  │ Frame 1: 33 MB           │ │ Frame 1: 33 MB                   │ │    │
│  │  │ ...                      │ │ ...                              │ │    │
│  │  │ Frame 11: 33 MB          │ │ Frame 11: 33 MB                  │ │    │
│  │  └──────────────────────────┘ └──────────────────────────────────┘ │    │
│  │              397 MB          +            397 MB                   │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ BRUSH TEXTURES                                       ~150 MB       │    │
│  │  Shape textures (50+), Grain textures (50+), Thumbnails           │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ CANVAS TEXTURE (Current frame being drawn)           ~33 MB        │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
                          TOTAL: ~2.97 GB
```

---

## Recommended Fixes

### Fix 1: Reduce Undo Snapshot Frequency (IMMEDIATE - HIGH IMPACT)

**Change**: Increase snapshot interval from 10 to 25-50 strokes

**File**: `src/vybe/app/drawing/native/metal_renderer.mm:2396-2397`

```cpp
// BEFORE:
tree->setMaxNodes(50);         // 50 undo levels per frame
tree->setSnapshotInterval(10); // Checkpoint every 10 strokes

// AFTER:
tree->setMaxNodes(50);         // 50 undo levels per frame
tree->setSnapshotInterval(25); // Checkpoint every 25 strokes (was 10)
```

**Impact**:
- Reduces snapshots from 5 to 2 per tree
- Saves: 12 × 3 × 33.1 MB = **~1.19 GB**
- Trade-off: Slower undo replay (must replay up to 24 strokes instead of 9)

### Fix 2: Remove CPU Frame Backup (IMMEDIATE - MEDIUM IMPACT)

**Rationale**: GPU cache is always available and faster. CPU backup is unused.

**File**: `DrawingMobile/drawing_mobile_ios.mm`

```cpp
// REMOVE this from framestore_save_current():
// CPU backup (slow - only for persistence)
uint8_t* pixels = nullptr;
int size = metal_stamp_capture_snapshot(&pixels);
if (pixels && size > 0) {
    g_frameStore.frames[frame].assign(pixels, pixels + size);
    metal_stamp_free_snapshot(pixels);
}

// OR: Don't pre-allocate CPU frames
// CHANGE from:
g_frameStore.frames.resize(FrameStore::MAX_FRAMES);
// TO:
// Don't resize, keep empty - only allocate on demand for persistence
```

**Impact**: Saves **397 MB**

### Fix 3: Add Memory Warning Handler (CRITICAL - SAFETY)

**File**: `DrawingMobile/drawing_mobile_ios.mm`

Add memory pressure monitoring:

```objc
#import <os/proc.h>

// Add at top of file
static void handle_memory_warning() {
    NSLog(@"[Memory] WARNING: Low memory detected!");

    // 1. Clear CPU frame backups (can regenerate from GPU)
    for (auto& frame : g_frameStore.frames) {
        frame.clear();
        frame.shrink_to_fit();
    }

    // 2. Reduce undo history for non-current frames
    for (int i = 0; i < (int)metal_stamp::g_undo_trees.size(); i++) {
        if (i != g_current_undo_frame && metal_stamp::g_undo_trees[i]) {
            // Keep only snapshots, clear stroke data
            metal_stamp::g_undo_trees[i]->compactMemory();
        }
    }

    // 3. Clear brush texture cache (can reload on demand)
    // TODO: implement lazy brush loading

    NSLog(@"[Memory] Freed emergency memory");
}

// Register for memory warnings (in main or init)
[[NSNotificationCenter defaultCenter]
    addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
    object:nil
    queue:[NSOperationQueue mainQueue]
    usingBlock:^(NSNotification *note) {
        handle_memory_warning();
    }];

// Also periodically check available memory
static void check_memory_pressure() {
    size_t available = os_proc_available_memory();
    if (available < 200 * 1024 * 1024) {  // Less than 200 MB
        handle_memory_warning();
    }
}
```

### Fix 4: Implement Delta Snapshots (MEDIUM-TERM - HIGH IMPACT)

The undo system already has delta snapshot support but it's **not being used**!

**File**: `src/vybe/app/drawing/native/undo_tree.hpp:80-85`

```cpp
struct CanvasSnapshot {
    // Delta snapshot support - only store changed region
    bool isDelta = false;         // true = delta (partial), false = full snapshot
    int deltaX = 0, deltaY = 0;   // Position of delta region
    // ...
};
```

**Implementation**: Track stroke bounding box and only snapshot that region.

**Estimated savings**: 50-90% of snapshot memory (most strokes affect <10% of canvas)

### Fix 5: Lazy Brush Loading (MEDIUM-TERM - MEDIUM IMPACT)

**Current**: All 50+ brushes loaded at startup
**Better**: Load brush textures on-demand, cache most recently used

```objc
// In brush_importer.mm
+ (void)applyBrush:(int32_t)brushId {
    ImportedBrush* brush = [self getBrushById:brushId];

    // Lazy load textures if not already loaded
    if (brush->shapeTextureId < 0 && brush->shapeURL) {
        brush->shapeTextureId = [self loadTextureFromURL:brush->shapeURL];
    }
    // ...
}

// Unload unused brush textures when memory pressure
+ (void)unloadUnusedBrushTextures {
    // Keep last 5 used brushes, unload rest
}
```

**Impact**: Could save ~100 MB on devices with many brushes

### Fix 6: Reduce Canvas Resolution for Lower-End Devices

**Current**: Always 3840×2160 (4K)
**Better**: Detect device and adjust

```objc
// In metal_renderer.mm
- (void)calculateOptimalCanvasSize {
    // Get device memory
    size_t totalMem = [NSProcessInfo processInfo].physicalMemory;

    if (totalMem < 4ULL * 1024 * 1024 * 1024) {  // Less than 4 GB
        // Use 2K canvas: 1920x1080
        self.canvasWidth = 1920;
        self.canvasHeight = 1080;
        // Saves ~75% memory on all canvas-related storage
    }
}
```

**Impact**: Reduces ALL canvas memory by 75% on low-memory devices

---

## Quick Wins Summary

| Fix | Memory Saved | Effort | Risk |
|-----|-------------|--------|------|
| Increase snapshot interval (10→25) | ~1.19 GB | 1 line change | Low (slower undo) |
| Remove CPU frame backup | ~397 MB | ~20 lines | Low (no persistence) |
| Add memory warning handler | Safety net | ~50 lines | None |
| Delta snapshots | ~1.5 GB | ~200 lines | Medium (complexity) |
| Lazy brush loading | ~100 MB | ~100 lines | Low |
| Adaptive canvas size | ~2 GB (on low-end) | ~50 lines | Medium (quality) |

---

## Monitoring & Debugging

### Add Memory Logging

```cpp
// In metal_renderer.mm
void log_memory_usage() {
    size_t total = 0;

    // Undo trees
    for (auto& tree : g_undo_trees) {
        if (tree) total += tree->getMemoryUsage();
    }
    std::cout << "[Memory] Undo trees: " << (total / 1024 / 1024) << " MB" << std::endl;

    // Frame cache
    size_t frameMem = 12 * 3840 * 2160 * 4;  // GPU cache
    std::cout << "[Memory] Frame cache: " << (frameMem / 1024 / 1024) << " MB" << std::endl;

    // iOS available memory
    size_t available = os_proc_available_memory();
    std::cout << "[Memory] iOS available: " << (available / 1024 / 1024) << " MB" << std::endl;
}
```

### Xcode Instruments

1. **Memory Graph Debugger**: Shows all allocations and retain cycles
2. **Allocations Instrument**: Track memory over time
3. **VM Tracker**: See Metal texture memory separately

---

## Testing Plan

1. **Baseline Test**:
   - Launch app, note initial memory (Xcode memory gauge)
   - Draw 50 strokes on each of 12 frames
   - Note memory after each frame switch
   - Expected: Memory should plateau, not grow unbounded

2. **Stress Test**:
   - Draw rapidly on all frames, cycling between them
   - Monitor for OOM warnings or crashes
   - Test on lowest-memory target device (iPad Air)

3. **Recovery Test**:
   - Trigger iOS memory warning (Debug → Simulate Memory Warning)
   - Verify app reduces memory and doesn't crash

---

## Files to Modify

1. **`src/vybe/app/drawing/native/metal_renderer.mm`**:
   - Line 2396-2397: Increase snapshot interval
   - Add `log_memory_usage()` function
   - Add memory warning callback support

2. **`DrawingMobile/drawing_mobile_ios.mm`**:
   - Remove CPU frame backup from `framestore_save_current()`
   - Add iOS memory warning observer
   - Add `os_proc_available_memory()` checks

3. **`src/vybe/app/drawing/native/undo_tree.cpp`**:
   - Add `compactMemory()` method for emergency memory reduction
   - Implement delta snapshot capture (future)

4. **`DrawingMobile/brush_importer.mm`**:
   - Add lazy texture loading (future)
   - Add `unloadUnusedBrushTextures()` method

---

## Immediate Action Items

1. [ ] **TODAY**: Change snapshot interval from 10 to 25 (1 line, saves ~1.2 GB)
2. [ ] **TODAY**: Add memory logging to monitor actual usage
3. [ ] **THIS WEEK**: Remove CPU frame backup (saves ~400 MB)
4. [ ] **THIS WEEK**: Add iOS memory warning handler
5. [ ] **LATER**: Implement delta snapshots
6. [ ] **LATER**: Lazy brush loading
