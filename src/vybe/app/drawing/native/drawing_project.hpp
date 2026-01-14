// drawing_project.hpp - Per-project instance management
// Encapsulates ALL state for a single drawing project:
// - Animation weave (threads, frames, strokes)
// - Undo trees (one per frame)
// - Canvas snapshot (for fast switching)
//
// No more globals - each project is a self-contained instance!

#pragma once

#include "animation_thread.h"
#include "undo_tree.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace drawing {

// =============================================================================
// Canvas State - Snapshot for fast project switching
// =============================================================================

struct CanvasState {
    std::vector<uint8_t> pixels;  // RGBA pixel data
    int width = 0;
    int height = 0;

    bool isValid() const { return !pixels.empty() && width > 0 && height > 0; }
    size_t byteSize() const { return pixels.size(); }
};

// =============================================================================
// Drawing Project - Complete state for one project
// =============================================================================

class DrawingProject {
public:
    DrawingProject();
    ~DrawingProject();

    // Disable copy (projects own unique resources)
    DrawingProject(const DrawingProject&) = delete;
    DrawingProject& operator=(const DrawingProject&) = delete;

    // Move is allowed
    DrawingProject(DrawingProject&&) = default;
    DrawingProject& operator=(DrawingProject&&) = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    // Initialize a new empty project with default weave
    void initNew();

    // Load from .vybed file
    bool loadFromFile(const std::string& path);

    // =========================================================================
    // File Operations
    // =========================================================================

    // Save to current file path (fails if isNewUnsaved())
    bool save();

    // Save to new path
    bool saveAs(const std::string& path);

    // Check dirty state
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void markClean() { dirty_ = false; }

    // =========================================================================
    // File Info
    // =========================================================================

    const std::string& getFilePath() const { return filePath_; }
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; markDirty(); }
    bool isNewUnsaved() const { return filePath_.empty(); }

    // =========================================================================
    // Per-Project State - The Core Data
    // =========================================================================

    // Animation data (threads, frames, strokes)
    animation::Weave weave;

    // Undo system - one tree per frame for independent undo history
    std::vector<std::unique_ptr<undo_tree::UndoTree>> undoTrees;
    int currentUndoFrame = 0;

    // Current stroke being drawn (accumulates points)
    undo_tree::StrokeData currentStroke;
    bool isRecordingStroke = false;

    // Bounding box tracking for current stroke
    float strokeMinX = 0, strokeMinY = 0;
    float strokeMaxX = 0, strokeMaxY = 0;
    bool strokeBboxValid = false;

    // Canvas state for quick restore when switching projects
    std::unique_ptr<CanvasState> canvasSnapshot;

    // =========================================================================
    // Undo Tree Management
    // =========================================================================

    // Initialize undo trees for given number of frames
    void initUndoTrees(int numFrames = 1);

    // Clean up all undo trees
    void cleanupUndoTrees();

    // Get undo tree for current frame
    undo_tree::UndoTree* getCurrentUndoTree();

    // Set which frame's undo tree is active
    void setCurrentUndoFrame(int frame);

    // Ensure undo tree exists for given frame (create if needed)
    void ensureUndoTreeForFrame(int frame);

    // Get total stroke count across all undo trees
    int getTotalStrokeCount() const;

private:
    std::string filePath_;  // Empty = new unsaved project
    std::string name_ = "Untitled";
    bool dirty_ = false;
};

// =============================================================================
// Project Manager - Singleton for managing project switching
// =============================================================================

class ProjectManager {
public:
    // Get singleton instance
    static ProjectManager& instance();

    // =========================================================================
    // Current Project Access
    // =========================================================================

    // Get currently active project (never null after init)
    DrawingProject* current() { return currentProject_.get(); }

    // Check if a project is active
    bool hasProject() const { return currentProject_ != nullptr; }

    // =========================================================================
    // Project Switching
    // =========================================================================

    // Create and switch to new empty project
    DrawingProject* createNew();

    // Load project from file and switch to it
    DrawingProject* loadAndSwitchTo(const std::string& path);

    // Switch to existing project (internal use)
    void switchTo(std::unique_ptr<DrawingProject> project);

    // =========================================================================
    // Canvas State Management
    // =========================================================================

    // Capture current canvas state into current project
    // Call this BEFORE switching to another project
    void captureCurrentCanvas();

    // Restore project's canvas state to the Metal renderer
    // Call this AFTER switching to a project
    void restoreCanvas();

    // Redraw canvas from weave strokes (if no snapshot available)
    void redrawFromWeave();

    // =========================================================================
    // Callbacks (set by metal_renderer/drawing_mobile)
    // =========================================================================

    using CaptureCallback = std::function<std::unique_ptr<CanvasState>()>;
    using RestoreCallback = std::function<void(const CanvasState&)>;
    using ClearCallback = std::function<void()>;

    void setCaptureCallback(CaptureCallback cb) { onCapture_ = cb; }
    void setRestoreCallback(RestoreCallback cb) { onRestore_ = cb; }
    void setClearCallback(ClearCallback cb) { onClear_ = cb; }

private:
    ProjectManager();
    ~ProjectManager() = default;

    // Disable copy/move
    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    std::unique_ptr<DrawingProject> currentProject_;

    // Callbacks for canvas operations (set by renderer)
    CaptureCallback onCapture_;
    RestoreCallback onRestore_;
    ClearCallback onClear_;
};

} // namespace drawing

// =============================================================================
// C API for integration with existing code
// =============================================================================

extern "C" {

// Project management
void drawing_project_init_new();
int drawing_project_load(const char* path);
int drawing_project_save();
int drawing_project_save_as(const char* path);
const char* drawing_project_get_path();
const char* drawing_project_get_name();
void drawing_project_set_name(const char* name);
int drawing_project_is_dirty();

// Project switching
void drawing_project_switch_to_new();
int drawing_project_switch_to_path(const char* path);

// Canvas state
void drawing_project_capture_canvas();
void drawing_project_restore_canvas();

} // extern "C"
