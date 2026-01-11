// Undo Tree - Emacs-style branching undo history for drawing
// Allows non-linear navigation through edit history with full branch preservation
//
// Key concepts:
// - Each stroke creates a new node in the tree
// - Undo moves to parent node
// - Making changes after undo creates a new branch (sibling)
// - All branches are preserved - never lose history
//
// Based on: https://www.dr-qubit.org/undo-tree.html

#ifndef UNDO_TREE_HPP
#define UNDO_TREE_HPP

#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <functional>

namespace undo_tree {

// =============================================================================
// Stroke Data - The "command" that can be replayed
// =============================================================================

struct StrokePoint {
    float x, y;           // Canvas coordinates
    float pressure;       // 0.0 - 1.0
    uint64_t timestamp;   // Milliseconds since stroke start (for animation!)
};

struct BrushSettings {
    // Core settings
    int brushType = 1;        // 0 = Round, 1 = Crayon, 2 = Watercolor, 3 = Marker
    float size = 20.0f;
    float hardness = 0.0f;
    float opacity = 1.0f;
    float spacing = 0.15f;
    float flow = 1.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    // Texture settings
    int32_t shape_texture_id = 0;
    int32_t grain_texture_id = 0;
    float grain_scale = 1.0f;
    bool grain_moving = true;
    int shape_inverted = 0;  // 0 = WHITE=opaque, 1 = BLACK=opaque

    // Dynamics
    float rotation = 0.0f;
    float rotation_jitter = 0.0f;
    float scatter = 0.0f;
    float size_pressure = 1.0f;
    float opacity_pressure = 0.0f;
    float size_velocity = 0.0f;
    float size_jitter = 0.0f;
    float opacity_jitter = 0.0f;
};

struct StrokeData {
    std::vector<StrokePoint> points;
    BrushSettings brush;
    uint64_t startTime;   // Absolute timestamp when stroke started
    uint32_t randomSeed;  // For deterministic replay of jitter/scatter

    bool isEmpty() const { return points.empty(); }
    size_t pointCount() const { return points.size(); }
};

// =============================================================================
// Canvas Snapshot - For fast restoration (supports delta snapshots)
// =============================================================================

struct CanvasSnapshot {
    std::vector<uint8_t> pixels;  // RGBA pixel data (full or delta region)
    int width, height;            // Canvas dimensions (for full) or region size (for delta)
    uint64_t timestamp;

    // Delta snapshot support - only store changed region
    bool isDelta = false;         // true = delta (partial), false = full snapshot
    int deltaX = 0, deltaY = 0;   // Top-left corner of delta region in canvas coords
    int canvasWidth = 0;          // Full canvas width (needed for delta restoration)
    int canvasHeight = 0;         // Full canvas height

    size_t byteSize() const { return pixels.size(); }

    // For delta: region is (deltaX, deltaY) to (deltaX+width, deltaY+height)
    // pixels contains only the changed region (width * height * 4 bytes)
};

// =============================================================================
// Undo Node - Single point in the history tree
// =============================================================================

struct UndoNode {
    uint64_t id;                      // Unique identifier
    uint64_t timestamp;               // When this action occurred
    UndoNode* parent;                 // Previous state (null for root)
    std::vector<UndoNode*> children;  // Next states (branches)
    int activeChildIndex;             // Which branch is "current" for redo

    // The stroke that created this state (empty for root)
    StrokeData stroke;

    // Optional snapshot for fast navigation (stored every N nodes)
    std::shared_ptr<CanvasSnapshot> snapshot;

    UndoNode()
        : id(0), timestamp(0), parent(nullptr), activeChildIndex(0) {}

    bool isRoot() const { return parent == nullptr; }
    bool hasChildren() const { return !children.empty(); }
    int branchCount() const { return static_cast<int>(children.size()); }

    // Get depth in tree (root = 0)
    int depth() const {
        int d = 0;
        const UndoNode* node = this;
        while (node->parent) {
            d++;
            node = node->parent;
        }
        return d;
    }
};

// =============================================================================
// Undo Tree - The complete history manager
// =============================================================================

class UndoTree {
public:
    // Callbacks for canvas operations
    using SnapshotCallback = std::function<std::shared_ptr<CanvasSnapshot>()>;
    using RestoreCallback = std::function<void(const CanvasSnapshot&)>;
    using ClearCallback = std::function<void()>;
    using ApplyStrokeCallback = std::function<void(const StrokeData&)>;

    UndoTree();
    ~UndoTree();

    // Configuration
    void setMaxNodes(int max) { maxNodes_ = max; }
    void setSnapshotInterval(int interval) { snapshotInterval_ = interval; }
    int getMaxNodes() const { return maxNodes_; }
    int getSnapshotInterval() const { return snapshotInterval_; }

    // Register callbacks for canvas operations
    void setSnapshotCallback(SnapshotCallback cb) { onSnapshot_ = cb; }
    void setRestoreCallback(RestoreCallback cb) { onRestore_ = cb; }
    void setClearCallback(ClearCallback cb) { onClear_ = cb; }
    void setApplyStrokeCallback(ApplyStrokeCallback cb) { onApplyStroke_ = cb; }

    // Core operations
    void recordStroke(const StrokeData& stroke);
    bool undo();                          // Returns false if at root
    bool redo();                          // Returns false if no children
    bool redoBranch(int branchIndex);     // Redo specific branch
    bool jumpToNode(uint64_t nodeId);     // Navigate to any node

    // Tree information
    UndoNode* getRoot() const { return root_; }
    UndoNode* getCurrent() const { return current_; }
    uint64_t getCurrentId() const { return current_ ? current_->id : 0; }
    int getTotalNodes() const { return totalNodes_; }
    int getCurrentDepth() const { return current_ ? current_->depth() : 0; }

    // Check if operations are possible
    bool canUndo() const { return current_ && current_->parent != nullptr; }
    bool canRedo() const { return current_ && !current_->children.empty(); }
    int getRedoBranchCount() const {
        return current_ ? current_->branchCount() : 0;
    }

    // Get all nodes for visualization
    std::vector<UndoNode*> getAllNodes() const;

    // Get path from root to a node
    std::vector<UndoNode*> getPathToNode(UndoNode* node) const;

    // Find node by ID
    UndoNode* findNode(uint64_t nodeId) const;

    // Statistics
    size_t getMemoryUsage() const;

    // Clear all history
    void clear();

    // Persistence (future)
    // bool saveToFile(const std::string& path);
    // bool loadFromFile(const std::string& path);

private:
    UndoNode* root_;
    UndoNode* current_;
    uint64_t nextNodeId_;
    int totalNodes_;

    // Settings
    int maxNodes_;          // Default: 250 (like Procreate)
    int snapshotInterval_;  // Store snapshot every N nodes (default: 25)

    // Callbacks
    SnapshotCallback onSnapshot_;
    RestoreCallback onRestore_;
    ClearCallback onClear_;
    ApplyStrokeCallback onApplyStroke_;

    // Internal helpers
    void restoreToNode(UndoNode* target);
    UndoNode* findNearestSnapshot(UndoNode* node) const;
    void trimOldestBranch();
    void deleteSubtree(UndoNode* node);
    void collectAllNodes(UndoNode* node, std::vector<UndoNode*>& result) const;
    UndoNode* findNodeRecursive(UndoNode* node, uint64_t nodeId) const;
};

} // namespace undo_tree

#endif // UNDO_TREE_HPP
