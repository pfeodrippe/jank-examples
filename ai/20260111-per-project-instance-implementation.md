# Per-Project Instance Implementation

**Date**: 2026-01-11

## What I Learned

### Problem
Global state in drawing code caused **state leakage between projects**. When switching projects in the gallery, undo trees and strokes from one project leaked into another.

### Solution: DrawingProject + ProjectManager

Created per-project instance architecture:

1. **DrawingProject class** (`drawing_project.hpp/.mm`)
   - Encapsulates ALL per-project state:
     - `animation::Weave weave` - animation data
     - `std::vector<std::unique_ptr<undo_tree::UndoTree>> undoTrees` - undo history
     - `undo_tree::StrokeData currentStroke` - stroke in progress
     - `std::unique_ptr<CanvasState> canvasSnapshot` - for fast switching
   - File operations: `loadFromFile()`, `save()`, `saveAs()`
   - Dirty tracking: `isDirty()`, `markDirty()`

2. **ProjectManager singleton**
   - Manages project switching: `createNew()`, `loadAndSwitchTo(path)`
   - Canvas capture/restore via callbacks
   - Single active project at a time

### Key Implementation Details

1. **Helper functions must be OUTSIDE namespace**
   - `metal_renderer.mm` has `namespace metal_stamp { }` block
   - `extern "C"` METAL_EXPORT functions are OUTSIDE this namespace
   - Helper functions like `get_current_project()` must be at file scope AFTER the namespace closes

2. **Xcode project configuration**
   - Updated `config-common.yml` to use directory pattern:
     ```yaml
     - path: ../src/vybe/app/drawing/native
       compilerFlags: ["-x", "objective-c++", "-fobjc-arc"]
       excludes:
         - "*.h"
         - "*.hpp"
         - "*.metal"
     ```
   - New files are automatically picked up!

3. **C API for integration**
   - `drawing_project_init_new()`, `drawing_project_load()`, etc.
   - Allows jank/Clojure code to call project management functions

## Files Modified

- `src/vybe/app/drawing/native/drawing_project.hpp` - **NEW** - class definitions
- `src/vybe/app/drawing/native/drawing_project.mm` - **NEW** - implementation
- `src/vybe/app/drawing/native/metal_renderer.mm` - removed globals, added helper functions
- `DrawingMobile/drawing_mobile_ios.mm` - uses ProjectManager for switching
- `DrawingMobile/config-common.yml` - directory-based source inclusion

## Commands Run

```bash
# Build and run iOS simulator app
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/ios_build.txt

# Check for errors
grep -i "error:" /tmp/ios_build.txt
```

## What's Next

1. **Test project switching in gallery**:
   - Create new project, draw strokes
   - Open gallery, tap "+" to create another project
   - Draw different strokes
   - Tap first project - verify its strokes appear (not second project's)

2. **Fix any remaining state leakage**:
   - Brush settings are still global (intentional - per-app not per-project)
   - Canvas transform might need per-project storage

3. **Implement canvas snapshot capture/restore** for fast project switching
