# Per-Project Instance Architecture

**Date**: 2026-01-11

## Problem Statement

Currently, drawing state is stored in **global variables** scattered across multiple files:

```
metal_renderer.mm:
  - g_undo_trees (vector of undo trees, one per frame)
  - g_current_undo_frame
  - g_current_stroke
  - g_is_recording_stroke
  - g_stroke_min/max_x/y, g_stroke_bbox_valid

animation_thread.mm:
  - g_weave (the animation data)
  - g_currentStroke (stroke in progress)
  - g_currentBrush, g_currentColor

drawing_mobile_ios.mm:
  - g_selectedBrushId, g_selectedBrushIndex
  - g_eraserMode
```

When switching between projects (gallery open, tap on different project), this global state **leaks between projects**. The workaround of "resetting globals" is fragile and error-prone.

## Solution: DrawingProject Class

Create a `DrawingProject` class that encapsulates ALL per-project state:

```cpp
// drawing_project.hpp
#pragma once

#include "animation_thread.h"
#include "undo_tree.hpp"
#include <memory>
#include <string>
#include <vector>

namespace drawing {

// Canvas snapshot for fast project switching
struct CanvasState {
    std::vector<uint8_t> pixels;  // RGBA
    int width, height;
};

class DrawingProject {
public:
    DrawingProject();
    ~DrawingProject();

    // Initialize a new empty project
    void initNew();

    // Load from file
    bool loadFromFile(const std::string& path);

    // Save to file
    bool save();
    bool saveAs(const std::string& path);

    // Check if project has unsaved changes
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void markClean() { dirty_ = false; }

    // File info
    const std::string& getFilePath() const { return filePath_; }
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    bool isNewUnsaved() const { return filePath_.empty(); }

    // =========================================================================
    // Per-Project State (the core data)
    // =========================================================================

    // Animation data (threads, frames, strokes)
    animation::Weave weave;

    // Undo system - one tree per frame
    std::vector<std::unique_ptr<undo_tree::UndoTree>> undoTrees;
    int currentUndoFrame = 0;

    // Current stroke being drawn
    undo_tree::StrokeData currentStroke;
    bool isRecordingStroke = false;

    // Bounding box tracking for current stroke
    float strokeMinX = 0, strokeMinY = 0;
    float strokeMaxX = 0, strokeMaxY = 0;
    bool strokeBboxValid = false;

    // Canvas state (for quick restore when switching projects)
    std::unique_ptr<CanvasState> canvasSnapshot;

    // =========================================================================
    // Undo Tree Management
    // =========================================================================

    void initUndoTrees(int numFrames = 1);
    void cleanupUndoTrees();
    undo_tree::UndoTree* getCurrentUndoTree();
    void setCurrentUndoFrame(int frame);
    void ensureUndoTreeForFrame(int frame);

private:
    std::string filePath_;  // Empty = new unsaved project
    std::string name_;
    bool dirty_ = false;
};

// =========================================================================
// Project Manager - Handles project switching
// =========================================================================

class ProjectManager {
public:
    static ProjectManager& instance();

    // Get current active project
    DrawingProject* current() { return currentProject_.get(); }

    // Create new empty project
    DrawingProject* createNew();

    // Load project from file (creates if not in cache)
    DrawingProject* loadProject(const std::string& path);

    // Switch to project (handles canvas save/restore)
    void switchTo(DrawingProject* project);
    void switchToNew();
    void switchToPath(const std::string& path);

    // Capture current canvas state into current project
    void captureCurrentCanvas();

    // Restore project's canvas state to renderer
    void restoreCanvas(DrawingProject* project);

    // Close project (remove from cache, prompt save if dirty)
    void closeProject(DrawingProject* project);

    // Get project by path (nullptr if not loaded)
    DrawingProject* getProjectByPath(const std::string& path);

private:
    ProjectManager() = default;

    std::unique_ptr<DrawingProject> currentProject_;

    // Optional: cache of recently used projects for fast switching
    // std::map<std::string, std::unique_ptr<DrawingProject>> projectCache_;
};

} // namespace drawing
```

## Implementation Steps

### Phase 1: Create DrawingProject Class

1. **Create `src/vybe/app/drawing/native/drawing_project.hpp`**
   - Define `DrawingProject` class with all per-project state
   - Define `ProjectManager` singleton

2. **Create `src/vybe/app/drawing/native/drawing_project.mm`**
   - Implement `DrawingProject` methods
   - Implement `ProjectManager` methods

### Phase 2: Migrate Global State

3. **Update `metal_renderer.mm`**
   - Remove global undo state: `g_undo_trees`, `g_current_undo_frame`, etc.
   - Add functions that operate on a `DrawingProject*`:
     ```cpp
     void metal_stamp_undo_begin_stroke_for_project(DrawingProject* proj, float x, float y, float pressure);
     void metal_stamp_undo_add_point_for_project(DrawingProject* proj, float x, float y, float pressure);
     void metal_stamp_undo_end_stroke_for_project(DrawingProject* proj);
     ```
   - Or: Keep the convenience wrappers that get project from `ProjectManager::instance().current()`

4. **Update `animation_thread.mm`**
   - Remove `g_weave`, `g_currentStroke`, `g_currentBrush`, `g_currentColor`
   - `getCurrentWeave()` returns `ProjectManager::instance().current()->weave`

5. **Update `file_manager_ios.mm`**
   - `ios_save_drawing()` saves current project
   - `ios_load_drawing()` uses `ProjectManager` to switch projects

### Phase 3: Update UI Code

6. **Update `drawing_mobile_ios.mm`**
   - Gallery "+" tap: `ProjectManager::instance().switchToNew()`
   - Gallery item tap: `ProjectManager::instance().switchToPath(path)`
   - Auto-save: `ProjectManager::instance().current()->save()`

### Phase 4: Canvas State Management

7. **Implement canvas snapshot capture/restore**
   - When switching away from project: capture Metal canvas to `project->canvasSnapshot`
   - When switching to project: restore from snapshot OR redraw from weave

## Key Design Decisions

### 1. Single Current Project (Simpler)
We keep ONE project active at a time, with optional caching:
```cpp
// Simple: just current project
std::unique_ptr<DrawingProject> currentProject_;

// Optional: cache recent projects for fast switching back
std::map<std::string, std::unique_ptr<DrawingProject>> projectCache_;
```

### 2. Canvas Snapshot Strategy
When switching projects:
```
Project A active -> Switch to Project B:
1. Capture Project A's canvas pixels to A.canvasSnapshot
2. Load/create Project B
3. If B.canvasSnapshot exists: restore pixels directly (fast)
4. Else: redraw all strokes from B.weave (slower but correct)
```

### 3. Undo Tree Lifetime
Each project has its own undo trees. When project is closed/unloaded:
- Save to .vybed if dirty
- Release undo tree memory
- Canvas snapshot can be kept if we want fast re-open

### 4. Brush Settings: Per-App or Per-Project?
Two options:
- **Per-Project**: Each project remembers its brush (matches file-based workflows)
- **Per-App**: Brush settings are global (matches "tool in hand" mental model)

**Recommendation**: Start with per-app (current behavior), optionally move to per-project later.

## Files to Create/Modify

### New Files:
- `src/vybe/app/drawing/native/drawing_project.hpp`
- `src/vybe/app/drawing/native/drawing_project.mm`

### Modified Files:
- `src/vybe/app/drawing/native/metal_renderer.mm` - Use ProjectManager
- `src/vybe/app/drawing/native/metal_renderer.h` - Add project-aware APIs
- `src/vybe/app/drawing/native/animation_thread.mm` - Remove globals, use ProjectManager
- `src/vybe/app/drawing/native/animation_thread.h` - Update API
- `src/vybe/app/drawing/native/file_manager_ios.mm` - Use ProjectManager for load/save
- `DrawingMobile/drawing_mobile_ios.mm` - Use ProjectManager for switching

## Migration Strategy

1. **Create DrawingProject with globals INSIDE it first**
   - Move the global variables INTO DrawingProject as members
   - Keep global accessor functions working via ProjectManager

2. **Update all call sites incrementally**
   - Functions like `metal_stamp_undo_begin_stroke()` internally get project from manager
   - No external API changes needed initially

3. **Test thoroughly**
   - Create new project, draw, open gallery - appears
   - Create another new project, draw, gallery shows both
   - Tap on first project - loads correctly with its strokes
   - Tap on second project - loads correctly with its strokes

## Expected Behavior After Implementation

1. **Create new project** -> Empty canvas, empty undo tree
2. **Draw strokes** -> Recorded in current project's undo trees
3. **Open gallery** -> Current project saved, gallery shows all projects
4. **Tap different project** ->
   - Current project's canvas captured to snapshot
   - New project loaded
   - New project's canvas restored
   - Undo trees are for NEW project, not old
5. **Tap "+"** -> Creates brand new empty project with fresh state
6. **No state leakage** -> Each project is completely isolated

## Commands to Build/Test

```bash
# Build and run iOS simulator
make drawing-ios-jit-sim-run 2>&1 | tee /tmp/ios_build.txt

# Check for compilation errors
grep -i "error:" /tmp/ios_build.txt
```
