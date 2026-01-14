// drawing_project.mm - Per-project instance implementation
#import <Foundation/Foundation.h>

#include "drawing_project.hpp"
#include "vybed_format.hpp"
#include "file_manager_ios.h"
#include <iostream>

namespace drawing {

// =============================================================================
// DrawingProject Implementation
// =============================================================================

DrawingProject::DrawingProject() {
    NSLog(@"[DrawingProject] Created new instance");
}

DrawingProject::~DrawingProject() {
    cleanupUndoTrees();
    NSLog(@"[DrawingProject] Destroyed instance: %s", name_.c_str());
}

void DrawingProject::initNew() {
    // Clear any existing state
    cleanupUndoTrees();
    weave.threads.clear();

    // Initialize fresh weave with one thread and one frame
    weave.threads.emplace_back();
    weave.threads[0].frames.emplace_back();

    // NOTE: Don't init undo trees here - let metal_stamp_undo_init_with_frames() do it
    // because it also configures the replay callbacks which are required for undo to work

    // Reset stroke state
    currentStroke = undo_tree::StrokeData();
    isRecordingStroke = false;
    strokeBboxValid = false;

    // Clear canvas snapshot
    canvasSnapshot.reset();

    // Reset file info
    filePath_.clear();
    name_ = "Untitled";
    dirty_ = false;

    NSLog(@"[DrawingProject] Initialized new empty project");
}

bool DrawingProject::loadFromFile(const std::string& path) {
    // Clear existing state first
    cleanupUndoTrees();
    weave.threads.clear();

    // Load from .vybed file
    std::string loadedName;
    bool success = vybe::drawing::load_vybed(path, weave, loadedName);

    if (!success) {
        NSLog(@"[DrawingProject] Failed to load from: %s", path.c_str());
        // Restore to empty state
        initNew();
        return false;
    }

    // Set file info
    filePath_ = path;
    name_ = loadedName;
    dirty_ = false;

    // Invalidate caches
    weave.invalidateAllCaches();

    // NOTE: Don't init undo trees here - let metal_stamp_undo_init_with_frames() do it
    // because it also configures the replay callbacks which are required for undo to work

    // Reset stroke state
    currentStroke = undo_tree::StrokeData();
    isRecordingStroke = false;
    strokeBboxValid = false;

    // Clear canvas snapshot (will be rebuilt on restore)
    canvasSnapshot.reset();

    NSLog(@"[DrawingProject] Loaded project: %s", name_.c_str());
    return true;
}

bool DrawingProject::save() {
    if (filePath_.empty()) {
        NSLog(@"[DrawingProject] Cannot save: no file path set");
        return false;
    }
    return saveAs(filePath_);
}

bool DrawingProject::saveAs(const std::string& path) {
    // TODO: Sync strokes from undo trees to weave before saving
    // This needs integration with the sync functions from metal_renderer

    bool success = vybe::drawing::save_vybed(path, weave, name_, nullptr, 0);

    if (success) {
        filePath_ = path;
        dirty_ = false;
        NSLog(@"[DrawingProject] Saved to: %s", path.c_str());
    } else {
        NSLog(@"[DrawingProject] Failed to save to: %s", path.c_str());
    }

    return success;
}

// =============================================================================
// Undo Tree Management
// =============================================================================

void DrawingProject::initUndoTrees(int numFrames) {
    cleanupUndoTrees();

    if (numFrames < 1) numFrames = 1;

    undoTrees.reserve(numFrames);
    for (int i = 0; i < numFrames; i++) {
        undoTrees.push_back(std::make_unique<undo_tree::UndoTree>());
    }

    currentUndoFrame = 0;
    NSLog(@"[DrawingProject] Initialized %d undo trees", numFrames);
}

void DrawingProject::cleanupUndoTrees() {
    undoTrees.clear();
    currentUndoFrame = 0;
}

undo_tree::UndoTree* DrawingProject::getCurrentUndoTree() {
    if (currentUndoFrame < 0 || currentUndoFrame >= static_cast<int>(undoTrees.size())) {
        return nullptr;
    }
    return undoTrees[currentUndoFrame].get();
}

void DrawingProject::setCurrentUndoFrame(int frame) {
    if (frame >= 0 && frame < static_cast<int>(undoTrees.size())) {
        currentUndoFrame = frame;
    }
}

void DrawingProject::ensureUndoTreeForFrame(int frame) {
    while (static_cast<int>(undoTrees.size()) <= frame) {
        undoTrees.push_back(std::make_unique<undo_tree::UndoTree>());
    }
}

int DrawingProject::getTotalStrokeCount() const {
    int total = 0;
    for (const auto& tree : undoTrees) {
        if (tree) {
            // Count nodes from root to current (each node = one stroke)
            auto* current = tree->getCurrent();
            while (current && !current->isRoot()) {
                total++;
                current = current->parent;
            }
        }
    }
    return total;
}

// =============================================================================
// ProjectManager Implementation
// =============================================================================

ProjectManager& ProjectManager::instance() {
    static ProjectManager instance;
    return instance;
}

ProjectManager::ProjectManager() {
    NSLog(@"[ProjectManager] Initialized");
}

DrawingProject* ProjectManager::createNew() {
    // Capture current canvas before switching
    captureCurrentCanvas();

    // Create new project
    auto newProject = std::make_unique<DrawingProject>();
    newProject->initNew();

    // Switch to it
    currentProject_ = std::move(newProject);

    // Clear canvas for new project
    if (onClear_) {
        onClear_();
    }

    NSLog(@"[ProjectManager] Created and switched to new project");
    return currentProject_.get();
}

DrawingProject* ProjectManager::loadAndSwitchTo(const std::string& path) {
    // Capture current canvas before switching
    captureCurrentCanvas();

    // Create and load project
    auto newProject = std::make_unique<DrawingProject>();
    if (!newProject->loadFromFile(path)) {
        NSLog(@"[ProjectManager] Failed to load project from: %s", path.c_str());
        return nullptr;
    }

    // Switch to it
    currentProject_ = std::move(newProject);

    // Restore canvas (either from snapshot or by redrawing)
    restoreCanvas();

    NSLog(@"[ProjectManager] Loaded and switched to: %s", path.c_str());
    return currentProject_.get();
}

void ProjectManager::switchTo(std::unique_ptr<DrawingProject> project) {
    // Capture current canvas before switching
    captureCurrentCanvas();

    // Switch
    currentProject_ = std::move(project);

    // Restore new project's canvas
    restoreCanvas();
}

void ProjectManager::captureCurrentCanvas() {
    if (!currentProject_ || !onCapture_) {
        return;
    }

    auto snapshot = onCapture_();
    if (snapshot && snapshot->isValid()) {
        currentProject_->canvasSnapshot = std::move(snapshot);
        NSLog(@"[ProjectManager] Captured canvas snapshot: %dx%d",
              currentProject_->canvasSnapshot->width,
              currentProject_->canvasSnapshot->height);
    }
}

void ProjectManager::restoreCanvas() {
    if (!currentProject_) {
        return;
    }

    // If we have a snapshot, restore it directly (fast)
    if (currentProject_->canvasSnapshot && currentProject_->canvasSnapshot->isValid() && onRestore_) {
        onRestore_(*currentProject_->canvasSnapshot);
        NSLog(@"[ProjectManager] Restored canvas from snapshot");
        return;
    }

    // Otherwise, redraw from weave (slower but correct)
    redrawFromWeave();
}

void ProjectManager::redrawFromWeave() {
    if (!currentProject_) {
        return;
    }

    // Clear canvas first
    if (onClear_) {
        onClear_();
    }

    // TODO: Replay all strokes from undo trees to rebuild canvas
    // This needs integration with metal_stamp_replay_stroke
    NSLog(@"[ProjectManager] Redrawing from weave (TODO: implement stroke replay)");
}

} // namespace drawing

// =============================================================================
// C API Implementation
// =============================================================================

extern "C" {

static char g_projectPath[1024] = {0};
static char g_projectName[256] = {0};

void drawing_project_init_new() {
    drawing::ProjectManager::instance().createNew();
}

int drawing_project_load(const char* path) {
    if (!path) return 0;
    auto* project = drawing::ProjectManager::instance().loadAndSwitchTo(path);
    return project ? 1 : 0;
}

int drawing_project_save() {
    auto* project = drawing::ProjectManager::instance().current();
    if (!project) return 0;
    return project->save() ? 1 : 0;
}

int drawing_project_save_as(const char* path) {
    if (!path) return 0;
    auto* project = drawing::ProjectManager::instance().current();
    if (!project) return 0;
    return project->saveAs(path) ? 1 : 0;
}

const char* drawing_project_get_path() {
    auto* project = drawing::ProjectManager::instance().current();
    if (!project) return "";
    strncpy(g_projectPath, project->getFilePath().c_str(), sizeof(g_projectPath) - 1);
    return g_projectPath;
}

const char* drawing_project_get_name() {
    auto* project = drawing::ProjectManager::instance().current();
    if (!project) return "Untitled";
    strncpy(g_projectName, project->getName().c_str(), sizeof(g_projectName) - 1);
    return g_projectName;
}

void drawing_project_set_name(const char* name) {
    auto* project = drawing::ProjectManager::instance().current();
    if (project && name) {
        project->setName(name);
    }
}

int drawing_project_is_dirty() {
    auto* project = drawing::ProjectManager::instance().current();
    return project ? (project->isDirty() ? 1 : 0) : 0;
}

void drawing_project_switch_to_new() {
    drawing::ProjectManager::instance().createNew();
}

int drawing_project_switch_to_path(const char* path) {
    if (!path) return 0;
    auto* project = drawing::ProjectManager::instance().loadAndSwitchTo(path);
    return project ? 1 : 0;
}

void drawing_project_capture_canvas() {
    drawing::ProjectManager::instance().captureCurrentCanvas();
}

void drawing_project_restore_canvas() {
    drawing::ProjectManager::instance().restoreCanvas();
}

int drawing_project_save_new() {
    auto* project = drawing::ProjectManager::instance().current();
    if (!project) {
        NSLog(@"[drawing_project_save_new] No current project");
        return 0;
    }

    // Ensure drawings directory exists
    if (!ios_ensure_drawings_directory()) {
        NSLog(@"[drawing_project_save_new] Failed to create drawings directory");
        return 0;
    }

    // Generate a new filename
    std::string filename = vybe::drawing::generate_vybed_filename();
    std::string fullPath = std::string(ios_get_drawings_path()) + "/" + filename;

    // Sync undo trees to weave before saving
    extern void metal_stamp_sync_undo_to_weave(animation::Weave* weave);
    metal_stamp_sync_undo_to_weave(&project->weave);

    // Save to the generated path
    bool success = project->saveAs(fullPath);
    if (success) {
        NSLog(@"[drawing_project_save_new] Saved to: %s", fullPath.c_str());
    } else {
        NSLog(@"[drawing_project_save_new] Failed to save");
    }
    return success ? 1 : 0;
}

} // extern "C"
