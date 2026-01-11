# iOS OOM (Out of Memory) Investigation

## Executive Summary

The DrawingMobile app is experiencing OOM crashes on iOS devices. After analyzing the codebase, I've identified **~2.4 GB of potential memory usage** from the current implementation, which far exceeds iOS memory limits.

## Memory Budget Analysis

### Canvas Resolution
- **Fixed at 4K**: 3840 × 2160 = 8,294,400 pixels
- **Per frame (RGBA)**: 8,294,400 × 4 bytes = **33.18 MB**

### Current Memory Allocation

| Component | Location | Size | Notes |
|-----------|----------|------|-------|
| Main Canvas Texture | GPU | 33 MB | `canvasTexture` in MetalStampRendererImpl |
| GPU Frame Cache | GPU | **398 MB** | 12 frames × 33 MB, `MTLStorageModePrivate` |
| CPU Frame Backup | CPU | **398 MB** | 12 frames × 33 MB, `std::vector<uint8_t>` in FrameStore |
| Undo Snapshots | CPU | **1.66 GB** | 50 nodes × 33 MB (interval=1 means EVERY stroke!) |
| Point Buffer | Shared | 0.4 MB | 10000 points × 40 bytes |
| Brush Textures | GPU | ~20-50 MB | Varies by brush count |
| Stroke Data | CPU | ~5 MB | Undo tree stroke points |

### Total Memory
- **GPU**: ~450 MB
- **CPU**: ~2.1 GB
- **Combined**: ~2.5 GB

### iOS Memory Limits
- iPad Pro: ~4-5 GB total RAM, ~1-2 GB available to apps
- iPad Air/Standard: ~3-4 GB total RAM, ~800 MB - 1.2 GB available
- iOS aggressively kills apps exceeding ~1 GB for foreground apps

## Critical Issues Identified

### 1. Undo Snapshot Interval = 1 (CRITICAL)
**Location**: `src/vybe/app/drawing/native/metal_renderer.mm:2299`
```cpp
g_undo_tree->setSnapshotInterval(1); // Snapshot every 10 strokes
```
The comment says 10 but the code says 1! Every single stroke creates a **33 MB full canvas snapshot**.

**Impact**: After 50 strokes, undo tree alone uses 1.66 GB.

**Fix**: Change to `setSnapshotInterval(10)` or higher.

### 2. Dual Frame Storage Redundancy
Both GPU and CPU store all 12 frames = 800 MB total.

**Location**:
- GPU: `metal_renderer.mm:1338-1360` (initFrameCache)
- CPU: `drawing_mobile_ios.mm:1265` (FrameStore.frames)

**Impact**: 398 MB redundant storage.

**Fix**: Use GPU cache as primary, CPU backup only for persistence/save.

### 3. 4K Canvas Resolution on All Devices
**Location**: `metal_renderer.mm:266-267`
```cpp
self.canvasWidth = 3840;
self.canvasHeight = 2160;
```

**Impact**: 33 MB per frame regardless of device capability.

**Fix**: Scale canvas to device capabilities:
- iPad Pro 12.9": 4K (3840×2160) = 33 MB
- iPad Pro 11": 2732×2048 = 22 MB
- iPad Air: 2360×1640 = 15 MB
- Or use 2048×2048 universally = 16 MB

### 4. No Memory Pressure Handling
iOS sends `didReceiveMemoryWarning` before killing apps, but we don't respond.

**Fix**: Add memory warning observer to:
- Clear non-visible frame textures from GPU cache
- Clear old undo snapshots
- Reduce texture quality

### 5. Texture Accumulation
Brush textures are loaded but never unloaded when switching brushes.

**Location**: `brush_importer.mm` and `drawing_mobile_ios.mm`

**Fix**: Implement LRU cache for brush textures, unload unused ones.

## Memory Reduction Strategies

### Immediate Fixes (Can Do Today)

1. **Fix Undo Snapshot Interval**
   ```cpp
   // Change from:
   g_undo_tree->setSnapshotInterval(1);
   // To:
   g_undo_tree->setSnapshotInterval(10);
   ```
   **Savings**: ~1.3 GB (from 50 snapshots to ~5)

2. **Reduce Max Undo Nodes**
   ```cpp
   // Change from:
   g_undo_tree->setMaxNodes(50);
   // To:
   g_undo_tree->setMaxNodes(20);
   ```
   **Savings**: Additional ~0.4 GB

3. **Lazy CPU Frame Loading**
   Only copy frames to CPU when needed (save/export), not during drawing.
   ```cpp
   // In framestore_save_current_fast() - remove CPU backup for now
   static void framestore_save_current_fast() {
       if (g_frameStore.gpuCacheReady) {
           metal_stamp_cache_frame_to_gpu(g_frameStore.currentFrame);
       }
       // DON'T do CPU backup here - save for export only
   }
   ```
   **Savings**: ~398 MB (no CPU frame backup during drawing)

### Medium-Term Fixes

4. **Device-Appropriate Canvas Size**
   ```cpp
   // In initWithWindow:
   CGFloat scale = [UIScreen mainScreen].scale;
   int maxDim = MAX(self.drawableWidth, self.drawableHeight);
   if (maxDim > 2732) {
       self.canvasWidth = 3840;
       self.canvasHeight = 2160;
   } else if (maxDim > 2048) {
       self.canvasWidth = 2732;
       self.canvasHeight = 2048;
   } else {
       self.canvasWidth = 2048;
       self.canvasHeight = 1536;
   }
   ```
   **Savings**: 50% on non-Pro devices

5. **Memory Warning Handler**
   ```objc
   [[NSNotificationCenter defaultCenter]
       addObserver:self
       selector:@selector(didReceiveMemoryWarning:)
       name:UIApplicationDidReceiveMemoryWarningNotification
       object:nil];

   - (void)didReceiveMemoryWarning:(NSNotification*)notification {
       NSLog(@"[Memory] Warning received - clearing caches");
       // Clear old undo snapshots
       // Clear non-current frame GPU textures
       // Compact texture cache
   }
   ```

6. **Delta Snapshots for Undo**
   Instead of full canvas snapshots, store only changed regions.
   **Note**: Already have `capture_delta_snapshot()` in API - just need to use it!
   ```cpp
   // In undo_tree, track stroke bounding box and only snapshot that region
   ```
   **Savings**: 80-95% reduction in undo memory

### Long-Term Improvements

7. **Texture Streaming**
   - Keep only active brush textures in memory
   - Load on demand, unload LRU when memory pressure

8. **Frame Compression**
   - Use LZ4 or similar fast compression for CPU frame backup
   - 4:1 typical compression for drawings with large empty areas

9. **GPU Memory Management**
   - Use `MTLStorageModeManaged` for frame cache where possible
   - Let Metal manage swapping

## Recommended Action Plan

### Phase 1: Immediate (Fix Today)
1. Change `setSnapshotInterval(1)` to `setSnapshotInterval(10)`
2. Reduce `setMaxNodes(50)` to `setMaxNodes(20)`
3. Remove CPU frame backup from `framestore_save_current_fast()`

**Expected result**: Memory usage drops from ~2.4 GB to ~500 MB

### Phase 2: This Week
4. Add memory warning handler
5. Implement device-appropriate canvas sizing
6. Add logging to track actual memory usage

### Phase 3: Next Sprint
7. Implement delta snapshots for undo
8. Add texture LRU cache
9. Consider frame compression for CPU backup

## Debugging Commands

```bash
# Monitor memory in simulator
xcrun simctl spawn booted log stream --level debug --predicate 'subsystem == "com.vybe.DrawingMobile-JIT-Sim"'

# Check memory footprint
instruments -t "Allocations" -D /tmp/alloc.trace <app>

# Quick memory check via Xcode
# Product -> Profile -> Allocations
```

## Files to Modify

1. `src/vybe/app/drawing/native/metal_renderer.mm`
   - Line 2298-2299: Fix snapshot interval
   - Line 266-267: Device-appropriate canvas sizing
   - Add memory warning handler

2. `DrawingMobile/drawing_mobile_ios.mm`
   - Line 1304-1322: Lazy CPU backup
   - Add memory logging

3. `src/vybe/app/drawing/native/undo_tree.cpp`
   - Implement delta snapshot support

## Testing Plan

1. Run on actual iPad device (simulator doesn't have same memory limits)
2. Draw 50+ strokes and monitor memory
3. Verify undo still works with higher snapshot interval
4. Test frame switching with GPU-only cache
5. Verify memory warning response
