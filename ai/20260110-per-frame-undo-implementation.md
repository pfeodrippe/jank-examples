# Per-Frame Undo System Implementation

## Summary

Implemented a per-frame undo system for the DrawingMobile iOS app, fixing critical memory bugs and enabling independent undo history for each animation frame.

## What I Learned

### Memory Bug Root Causes
1. **Snapshot Interval = 1**: The code had `setSnapshotInterval(1)` but comment said "every 10 strokes" - this caused 33MB full canvas snapshots per stroke!
2. **Single Global Undo Tree**: All 12 frames shared one undo tree, causing undo on frame 2 to potentially undo strokes from frame 1
3. **Missing begin_stroke/end_stroke**: Stroke replay only called `add_stroke_point()` in a loop, missing proper stroke initialization

### Per-Frame Undo Architecture
- Changed from single `g_undo_tree` to `std::vector<std::unique_ptr<undo_tree::UndoTree>> g_undo_trees`
- Used `std::vector` instead of `std::array` to support configurable frame count (future multi-animation support)
- Added `get_current_undo_tree()` helper that validates frame index and returns current tree
- Frame switching via `metal_stamp_undo_set_frame(int frame)` called from `framestore_load_frame()`

### Memory Reduction
- **Before**: ~2.4 GB (50 snapshots × 33 MB = 1.66 GB for undo alone)
- **After**: ~200-400 MB expected (10 snapshots × 33 MB × fraction of frames used)

## Commands I Ran

```bash
# Build and run iOS simulator
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/drawing_ios_build.txt

# Check make targets
grep -E "^drawing.*:" Makefile | head -20
```

## Files Modified

### src/vybe/app/drawing/native/metal_renderer.mm
- Changed from single `g_undo_tree` to vector of undo trees
- Added `g_current_undo_frame` and `g_undo_initialized` state
- Added `get_current_undo_tree()` helper function
- Added `configure_undo_tree()` to setup each tree with snapshot interval=10 and callbacks
- Implemented `metal_stamp_undo_init_with_frames(int num_frames)` for configurable frame count
- Implemented `metal_stamp_undo_set_frame(int frame)`, `metal_stamp_undo_get_frame()`, `metal_stamp_undo_get_frame_count()`
- Updated all undo API functions to use `get_current_undo_tree()` instead of `g_undo_tree`
- Fixed stroke replay to use `begin_stroke()` + `add_stroke_point()` loop + `end_stroke()`
- Fixed clear color from pure white (1,1,1,1) to paper off-white (0.95, 0.95, 0.92, 1.0)

### src/vybe/app/drawing/native/metal_renderer.h
- Added new function declarations:
  - `void metal_stamp_undo_init_with_frames(int num_frames)`
  - `void metal_stamp_undo_set_frame(int frame)`
  - `int metal_stamp_undo_get_frame()`
  - `int metal_stamp_undo_get_frame_count()`

### DrawingMobile/drawing_mobile_ios.mm
- Added `metal_stamp_undo_set_frame(index)` call in `framestore_load_frame()` to sync undo tree with frame
- Changed init from `metal_stamp_undo_init()` to `metal_stamp_undo_init_with_frames(FrameStore::MAX_FRAMES)`

### ai/20260110-efficient-undo-plan.md
- Updated plan document to reflect actual implementation using `std::vector`
- Added code examples for new API and FrameStore integration

## Key API Changes

```cpp
// Initialize with configurable frame count
metal_stamp_undo_init_with_frames(FrameStore::MAX_FRAMES);

// Switch undo tree when changing animation frames
static void framestore_load_frame(int index) {
    metal_stamp_undo_set_frame(index);  // NEW!
    // ... rest of frame loading
}

// Query current undo state
int frame = metal_stamp_undo_get_frame();
int count = metal_stamp_undo_get_frame_count();
```

## What's Next

1. **Test on device**: Verify memory usage is acceptable on actual iPad
2. **Memory pressure handling**: Add `didReceiveMemoryWarning` observer to clear old undo snapshots
3. **Delta snapshots**: Future optimization to capture only stroke bounding box instead of full canvas
4. **LZ4 compression**: Add compression for checkpoint snapshots (~4:1 compression for drawings)

## Testing Notes - VERIFIED WORKING!

### Test Procedure Executed:
1. **Frame 0**: Drew 2 strokes → undo depth = 2
2. **Screenshot**: Confirmed 2 vertical strokes visible
3. **Frame 1**: Switched frame, drew 2 different strokes → undo depth = 2 (independent!)
4. **Screenshot**: Confirmed 2 horizontal strokes visible
5. **Frame 0**: Went back, undid once → undo depth = 1
6. **Screenshot**: Confirmed only 1 stroke remaining
7. **Frame 1**: Switched to frame 1, undid once → undo depth went from 2 to 1 (was untouched!)
8. **Screenshot**: Confirmed only 1 stroke remaining

### Key Verification:
- When we returned to frame 1 after undoing on frame 0, **frame 1 still had undo depth 2**
- This proves each frame has completely independent undo history
- The `metal_stamp_undo_set_frame()` correctly switches undo trees when frames change

### Test Commands Used:
```clojure
(require '[vybe.app.drawing.metal :as m])

;; Draw on frame 0
(m/draw-line! 500 400 1500 400)
(m/draw-line! 500 600 1500 600)
(m/undo-depth)  ; => 2

;; Switch to frame 1 and draw
(m/frame-next!)
(m/undo-frame)  ; => 1 (switched!)
(m/draw-line! 800 300 800 800)
(m/draw-line! 1000 300 1000 800)

;; Go back to frame 0 and undo
(m/frame-prev!)
(m/undo!)       ; depth: 2 -> 1

;; Go to frame 1 and verify independent
(m/frame-next!)
(m/undo-depth)  ; => 2 (untouched by frame 0 undo!)
(m/undo!)       ; depth: 2 -> 1
```

## Comprehensive Test 2 - User Requested Thorough Testing

### Test: "Draw 3 lines first, then 5 lines in the second, do 2 undos there and 1 undo in the first"

**Executed via nREPL with iOS Simulator screenshots for visual verification:**

1. **Frame 0: Drew 3 horizontal lines**
   - Commands: `(draw-line 300 500 600 500)`, `(draw-line 300 600 600 600)`, `(draw-line 300 700 600 700)`
   - Undo depth: 3
   - Screenshot: ✓ 3 lines visible

2. **Frame 1: Drew 5 horizontal lines**
   - Commands: 5 `draw-line` calls at y positions 500-900
   - Undo depth: 5
   - Screenshot: ✓ 5 lines visible

3. **Frame 1: Undid 2 times**
   - Undo depth: 5 → 4 → 3
   - Screenshot: ✓ 3 lines remaining

4. **Frame 0: Undid 1 time**
   - When switched to frame 0, depth was still 3 (unchanged while we were on frame 1!)
   - After undo: 3 → 2
   - Screenshot: ✓ 2 lines remaining

5. **Final State Verification**
   - Frame 0: depth = 2 ✓
   - Frame 1: depth = 4 (3 from test + 1 additional draw during verification)

### Undo/Redo Wild Testing

Also tested rapid undo/redo sequence on Frame 1:
- Depth: 4 → 3 (undo) → 4 (redo) → 1 (3 undos)
- Screenshot showed 1 line when depth was 1 ✓
- Redo 3 times: depth back to 4 ✓

### Key Conclusions:
1. **Per-frame undo isolation CONFIRMED**: Operations on one frame do not affect other frames
2. **Undo/redo within frame works correctly**: Visual matches undo depth
3. **Frame state preserved during switches**: Frame 0 kept depth 3 while we did undos on Frame 1
4. **Multiple undo/redo cycles work**: No corruption or state loss after many operations
