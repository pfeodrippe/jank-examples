# Fix: New Projects Not Appearing in Gallery After Drawing

**Date**: 2026-01-11

## What I Learned

### Root Cause
Strokes are stored in **two separate systems**:
1. **Undo Tree** (`g_undo_trees`) - Used during drawing for undo/redo functionality
2. **Animation Weave** (`animation::Weave::frame.strokes`) - Used for saving to .vybed format

The bug: `autoSaveCurrentDrawing()` checked `frame.strokes.empty()` which was **always true** because strokes go to the undo tree during drawing, not directly to the weave.

### Solution
Added two bridge functions in `metal_renderer.mm` to sync data from undo tree to weave before saving:

1. `metal_stamp_get_total_stroke_count_all_frames()` - Counts strokes across all undo trees
2. `metal_stamp_sync_undo_to_weave(animation::Weave* weave)` - Collects strokes from undo trees and populates the weave

### Key Code Paths
- `metal_stamp_undo_end_stroke()` records strokes to undo tree (not weave)
- `undo_tree::UndoTree` stores strokes in `UndoNode::stroke` along path from root to current
- `save_vybed()` serializes from weave, so weave must be populated first

## Files Modified

1. **`src/vybe/app/drawing/native/metal_renderer.mm`**
   - Added `#include "animation_thread.h"` (was missing - caused incomplete type error)
   - Added `metal_stamp_get_total_stroke_count_all_frames()` function
   - Added `metal_stamp_sync_undo_to_weave()` function

2. **`src/vybe/app/drawing/native/metal_renderer.h`**
   - Added declarations for new export functions

3. **`DrawingMobile/drawing_mobile_ios.mm`**
   - Updated `autoSaveCurrentDrawing()` to use new sync functions before saving

## Commands I Ran

```bash
# Build and run iOS simulator
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/ios_build.txt

# Check for compilation errors
grep -i "error:" /tmp/ios_build.txt

# Check if simulator is booted
xcrun simctl list | grep -i "booted"

# Check if app is installed
xcrun simctl listapps booted | grep -i drawing

# Check if app is running
xcrun simctl spawn booted launchctl list | grep -i drawing
```

## Build Error Fixed

Initial build failed with:
```
error: member access into incomplete type 'animation::Weave'
```

Fix: Added `#include "animation_thread.h"` to `metal_renderer.mm` since only a forward declaration existed.

## Architecture Notes

```
Drawing Flow:
  touch -> metal_stamp_undo_end_stroke() -> UndoTree (for undo/redo)

Save Flow (now fixed):
  autoSaveCurrentDrawing()
    -> metal_stamp_get_total_stroke_count_all_frames() (check if anything to save)
    -> metal_stamp_sync_undo_to_weave(&weave) (copy strokes to weave)
    -> save_vybed() (serialize weave to disk)
```

## What's Next

- User should test: create new project, draw, open gallery - project should appear
- Monitor for any edge cases (multiple frames, undo after save, etc.)
