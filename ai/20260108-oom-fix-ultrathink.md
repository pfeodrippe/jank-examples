# OOM (Out of Memory) Fix - Ultra Analysis

## Date: 2026-01-08

## Problem
App crashes with OOM on device when drawing. The undo system stores full canvas snapshots that consume too much memory.

## Root Cause Analysis

### Memory Calculation

**Current Settings:**
```cpp
g_undo_tree->setMaxNodes(10);       // 10 undo levels
g_undo_tree->setSnapshotInterval(1); // Snapshot EVERY stroke
```

**Canvas Size (iPad Pro 13-inch):**
- Screen: ~2732 × 2048 pixels (or similar high-res)
- Each pixel: 4 bytes (RGBA)
- **Single snapshot: 2732 × 2048 × 4 = ~22 MB**

**Total Memory for Undo:**
- 10 snapshots × 22 MB = **~220 MB just for undo!**
- Plus the actual canvas texture (~22 MB)
- Plus Metal buffers, brush textures, etc.
- **Total: 300+ MB easily**

iOS devices have strict memory limits. iPads typically kill apps using >1GB, but memory pressure can trigger kills much earlier.

### The Core Issue

Storing a **full canvas snapshot for every single stroke** is extremely wasteful:
1. Drawing 10 strokes = 10 × 22MB = 220MB
2. Most strokes only change a tiny portion of the canvas
3. Full snapshots are redundant when changes are localized

## Solution Options

### Option 1: Increase Snapshot Interval (Quick Fix)
```cpp
g_undo_tree->setMaxNodes(50);        // More undo levels
g_undo_tree->setSnapshotInterval(10); // Snapshot every 10 strokes
```
- Store snapshots every 10 strokes, not every stroke
- Replay strokes between snapshots for undo
- Memory: 5 snapshots × 22MB = 110MB (vs 220MB)
- **Requires stroke replay to work properly**

### Option 2: Delta Snapshots (Better Fix)
Only store the **changed region** after each stroke:
1. Track bounding box of each stroke
2. Only capture that rectangular region
3. Typical stroke might be 200×500 pixels = 400KB vs 22MB

**Memory savings: 50-100x reduction**

Already have infrastructure for this in `CanvasSnapshot`:
```cpp
bool isDelta = false;         // true = delta (partial), false = full snapshot
int deltaX = 0, deltaY = 0;   // Top-left corner of delta region
```

### Option 3: Compressed Snapshots
Compress snapshot data using LZ4 or similar:
- Canvas data compresses well (large uniform areas)
- Typical 3-5x compression ratio
- LZ4 is fast enough for real-time use

### Option 4: Reduce Snapshot Resolution
Store snapshots at 1/2 or 1/4 resolution:
- 1/2 resolution: 22MB → 5.5MB per snapshot
- Quality loss on undo, but acceptable for many use cases

### Option 5: Hybrid Approach (Recommended)
Combine multiple strategies:
1. **Full snapshot every N strokes** (e.g., every 25)
2. **Delta snapshots for intermediate strokes**
3. **Replay strokes when no snapshot available**

Memory budget: ~50-100MB for undo system

## Recommended Implementation

### Immediate Fix (Low Risk)
Change settings in `metal_renderer.mm`:

```cpp
// Before (OOM causing):
g_undo_tree->setMaxNodes(10);
g_undo_tree->setSnapshotInterval(1);

// After (memory efficient):
g_undo_tree->setMaxNodes(30);         // More undo levels
g_undo_tree->setSnapshotInterval(5);  // Snapshot every 5 strokes
```

This reduces snapshots from 10 to 6 (30/5), cutting memory ~40%.

**BUT: This requires stroke replay to work.** Need to verify `onApplyStroke_` callback is set.

### Better Fix: Disable Snapshots, Use Stroke Replay Only
```cpp
g_undo_tree->setMaxNodes(100);        // Many undo levels
g_undo_tree->setSnapshotInterval(0);  // NO snapshots
```

Store only stroke data (tiny: ~1-10KB per stroke), replay from root.
- 100 strokes × 10KB = 1MB total
- Undo is slower (must replay) but memory-safe

### Best Fix: Delta Snapshots
Modify `capture_canvas_snapshot()` to accept a bounding box and only capture that region.

## Files to Modify

1. `src/vybe/app/drawing/native/metal_renderer.mm` - Change snapshot settings
2. `src/vybe/app/drawing/native/undo_tree.cpp` - Ensure stroke replay works
3. `src/vybe/app/drawing/native/metal_renderer.h` - Add delta capture method (if implementing delta snapshots)

## Quick Fix Implementation

In `metal_renderer.mm`, change the undo init:

```cpp
METAL_EXPORT void metal_stamp_undo_init() {
    using namespace metal_stamp;
    if (g_undo_tree) return;

    g_undo_tree = std::make_unique<undo_tree::UndoTree>();

    // Memory-efficient settings
    g_undo_tree->setMaxNodes(50);         // 50 undo levels
    g_undo_tree->setSnapshotInterval(10); // Snapshot every 10 strokes

    // ... rest of callbacks ...
}
```

And ensure stroke replay callback is set:
```cpp
g_undo_tree->setApplyStrokeCallback([](const undo_tree::StrokeData& stroke) {
    // Replay stroke using existing brush engine
    metal_stamp_replay_stroke(stroke);
});
```

## Testing

After fix:
1. Draw 20+ strokes on device
2. Monitor memory in Xcode Instruments
3. Verify undo/redo works correctly
4. Check no OOM crashes

## Summary

| Approach | Memory | Undo Speed | Complexity |
|----------|--------|------------|------------|
| Current (snapshot every stroke) | ~220MB | Fast | Simple |
| Snapshot every 10 | ~66MB | Medium | Low |
| Stroke replay only | ~1MB | Slow | Low |
| Delta snapshots | ~10-20MB | Fast | High |
| Hybrid | ~30-50MB | Fast | Medium |

**Recommendation: Start with "Snapshot every 10" as immediate fix, then implement delta snapshots for production.**

---

## Implementation Done

### Changes Made

1. **Reduced snapshot frequency** in `metal_renderer.mm`:
   ```cpp
   g_undo_tree->setMaxNodes(50);        // 50 undo levels (was 10)
   g_undo_tree->setSnapshotInterval(10); // Snapshot every 10 strokes (was 1)
   ```

2. **Implemented stroke replay callback** - The callback was missing! Now strokes between snapshots are replayed:
   ```cpp
   g_undo_tree->setApplyStrokeCallback([](const undo_tree::StrokeData& stroke) {
       // Save current brush, apply stroke's brush, replay points, restore brush
   });
   ```

### Memory Impact

| Before | After |
|--------|-------|
| 10 snapshots × 22MB = 220MB | 5 snapshots × 22MB = 110MB |
| Every stroke snapshotted | Every 10th stroke snapshotted |
| No stroke replay | Stroke replay between snapshots |

### Files Modified

- `src/vybe/app/drawing/native/metal_renderer.mm`
  - Changed maxNodes: 10 → 50
  - Changed snapshotInterval: 1 → 10
  - Added `setApplyStrokeCallback` for stroke replay
