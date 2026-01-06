# Undo Tree Implementation - Emacs-style Branching History

**Date**: 2025-01-05

## What I Learned

### 1. Emacs-style Undo Tree Concepts
- Traditional undo is linear: A -> B -> C, then undo to B and make change D loses C forever
- Undo tree preserves ALL branches: when you undo to B and add D, C becomes a sibling branch
- Each stroke creates a new node in the tree
- Navigation: undo moves to parent, redo moves to active child, can switch branches

### 2. Data Structures Implemented

**StrokePoint**: x, y, pressure, timestamp
**StrokeData**: vector of points + brush settings + start time
**CanvasSnapshot**: RGBA pixel data for fast restoration
**UndoNode**: id, timestamp, parent, children, activeChildIndex, stroke, optional snapshot
**UndoTree**: root, current pointer, callbacks for canvas operations

### 3. Key Algorithms

- **Recording stroke**: Create new node, link to current as child, move current to new node
- **Undo**: Move current to parent, restore canvas state
- **Redo**: Move current to children[activeChildIndex], apply that stroke
- **Branch switching**: Change activeChildIndex before redo
- **State restoration**: Find nearest snapshot, replay strokes from there to target

### 4. Integration Challenges

- **Namespace scoping**: Variables in `metal_stamp` namespace needed `using namespace metal_stamp;` in extern "C" functions
- **Single translation unit**: Had to `#include "undo_tree.cpp"` in metal_renderer.mm since Xcode project doesn't include it
- **Gesture detection**: Two-finger tap (undo) and three-finger tap (redo) based on duration (<200ms) and movement (<20px)
- **CRITICAL BUG**: Undo-aware stroke functions must ALSO call the actual drawing functions, not just record data!

### 5. Memory Strategy (Hybrid)
- Store snapshot every N strokes (default: 25)
- Store stroke commands between snapshots
- Max replay = N strokes
- With 250 max nodes: ~40MB memory (10 snapshots * 4MB each)

## Commands I Ran

```bash
# Build and run iOS simulator app - USE THIS COMMAND!
make drawing-ios-jit-sim-run

# Manual app install when makefile had issues
xcrun simctl uninstall 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
xcrun simctl install 'iPad Pro 13-inch (M4)' "$(find ~/Library/Developer/Xcode/DerivedData -name 'DrawingMobile-JIT-Sim.app' -type d | head -1)"
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim

# Clean build when needed
cd /Users/pfeodrippe/dev/something/DrawingMobile
xcodebuild clean build -project DrawingMobile-JIT-Sim.xcodeproj -scheme DrawingMobile-JIT-Sim -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)'

# Check build errors
grep -A 20 "Undefined symbols" /tmp/drawing_build.txt
```

## Files Created/Modified

### New Files
- `src/vybe/app/drawing/native/undo_tree.hpp` - Header with all data structures and UndoTree class
- `src/vybe/app/drawing/native/undo_tree.cpp` - Full implementation
- `ai/20250105-undo-tree-implementation-plan.md` - Detailed implementation plan

### Modified Files
- `src/vybe/app/drawing/native/metal_renderer.h` - Added undo tree C API declarations
- `src/vybe/app/drawing/native/metal_renderer.mm` - Added undo tree API implementation, included undo_tree.cpp
- `DrawingMobile/drawing_mobile_ios.mm` - Added gesture handling for undo/redo, three-finger tracking

## Key Code Patterns

### C API for Undo Tree
```cpp
// In metal_renderer.h
void metal_stamp_undo_init();
bool metal_stamp_undo();
bool metal_stamp_redo();
bool metal_stamp_can_undo();
bool metal_stamp_can_redo();
void metal_stamp_undo_begin_stroke(float x, float y, float pressure);
void metal_stamp_undo_add_stroke_point(float x, float y, float pressure);
void metal_stamp_undo_end_stroke();
```

### Tap Detection for Gestures
```cpp
const Uint64 TAP_MAX_DURATION_MS = 200;
const float TAP_MAX_MOVEMENT = 20.0f;  // pixels

if (gestureDuration < TAP_MAX_DURATION_MS && maxMove < TAP_MAX_MOVEMENT) {
    // Two-finger tap = undo
    // Three-finger tap = redo
}
```

### Using Namespace in extern "C"
```cpp
METAL_EXPORT bool metal_stamp_undo() {
    using namespace metal_stamp;  // Access g_undo_tree, etc.
    if (!g_undo_tree) return false;
    return g_undo_tree->undo();
}
```

### CRITICAL: Undo functions must also draw!
```cpp
// WRONG - only records, doesn't draw
void metal_stamp_undo_begin_stroke(float x, float y, float pressure) {
    // Only recording logic...
}

// CORRECT - draws AND records
void metal_stamp_undo_begin_stroke(float x, float y, float pressure) {
    using namespace metal_stamp;
    // ALWAYS draw first!
    if (g_metal_renderer) {
        g_metal_renderer->begin_stroke(x, y, pressure);
    }
    // Then record to undo tree...
}
```

## What's Next

1. **Canvas Snapshot Capture**: Implement actual snapshot capture for fast undo navigation
2. **Visual Tree Display**: Show undo tree graphically as UI overlay
3. **Persistence**: Save/load undo tree with drawing files
4. **Animation Recording**: Store timestamps for playback (Loom-style)

## Build Command Reminder

**IMPORTANT**: Use `make drawing-ios-jit-sim-run` for building the drawing iOS app!

## Build Monitoring Reminder

**IMPORTANT**: Check build logs every 90 seconds, NOT 5 minutes! Use:
```bash
tail -100 /tmp/ios_build.txt
```
