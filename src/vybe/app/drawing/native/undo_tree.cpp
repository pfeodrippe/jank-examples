// Undo Tree Implementation
// Emacs-style branching undo history for drawing

#include "undo_tree.hpp"
#include <algorithm>
#include <iostream>

namespace undo_tree {

// =============================================================================
// Constructor / Destructor
// =============================================================================

UndoTree::UndoTree()
    : root_(nullptr)
    , current_(nullptr)
    , nextNodeId_(0)
    , totalNodes_(0)
    , maxNodes_(250)        // Procreate default
    , snapshotInterval_(25) // Store snapshot every 25 strokes
{
    // Create root node (represents empty canvas)
    root_ = new UndoNode();
    root_->id = nextNodeId_++;
    root_->timestamp = 0;
    current_ = root_;
    totalNodes_ = 1;
}

UndoTree::~UndoTree() {
    clear();
}

// =============================================================================
// Core Operations
// =============================================================================

void UndoTree::recordStroke(const StrokeData& stroke) {
    if (stroke.isEmpty()) return;

    // Create new node
    UndoNode* newNode = new UndoNode();
    newNode->id = nextNodeId_++;
    newNode->timestamp = stroke.startTime;
    newNode->stroke = stroke;
    newNode->parent = current_;

    // Link to parent as new child (creates branch if we're not at a leaf)
    current_->children.push_back(newNode);
    current_->activeChildIndex = static_cast<int>(current_->children.size()) - 1;

    // Move current to new node
    current_ = newNode;
    totalNodes_++;

    // Store snapshot periodically for fast navigation
    if (snapshotInterval_ > 0 && current_->depth() % snapshotInterval_ == 0) {
        if (onSnapshot_) {
            current_->snapshot = onSnapshot_();
            std::cout << "[UndoTree] Stored snapshot at depth " << current_->depth() << std::endl;
        }
    }

    // Trim if we exceeded max nodes
    while (totalNodes_ > maxNodes_) {
        trimOldestBranch();
    }

    std::cout << "[UndoTree] Recorded stroke (node " << newNode->id
              << ", depth " << newNode->depth()
              << ", total " << totalNodes_ << ")" << std::endl;
}

bool UndoTree::undo() {
    if (!canUndo()) {
        std::cout << "[UndoTree] Cannot undo - at root" << std::endl;
        return false;
    }

    // Move to parent
    current_ = current_->parent;

    // Restore canvas to this state
    restoreToNode(current_);

    std::cout << "[UndoTree] Undo to node " << current_->id
              << " (depth " << current_->depth() << ")" << std::endl;
    return true;
}

bool UndoTree::redo() {
    if (!canRedo()) {
        std::cout << "[UndoTree] Cannot redo - no children" << std::endl;
        return false;
    }

    // Move to active child
    current_ = current_->children[current_->activeChildIndex];

    // Restore canvas to this state
    restoreToNode(current_);

    std::cout << "[UndoTree] Redo to node " << current_->id
              << " (branch " << (current_->parent->activeChildIndex + 1)
              << "/" << current_->parent->branchCount() << ")" << std::endl;
    return true;
}

bool UndoTree::redoBranch(int branchIndex) {
    if (!current_ || branchIndex < 0 || branchIndex >= current_->branchCount()) {
        return false;
    }

    // Switch to specified branch
    current_->activeChildIndex = branchIndex;

    // Then redo
    return redo();
}

bool UndoTree::jumpToNode(uint64_t nodeId) {
    UndoNode* target = findNode(nodeId);
    if (!target) {
        std::cout << "[UndoTree] Node " << nodeId << " not found" << std::endl;
        return false;
    }

    if (target == current_) {
        return true; // Already there
    }

    restoreToNode(target);
    current_ = target;

    std::cout << "[UndoTree] Jumped to node " << nodeId << std::endl;
    return true;
}

// =============================================================================
// State Restoration
// =============================================================================

void UndoTree::restoreToNode(UndoNode* target) {
    if (!target) return;

    // Find nearest ancestor with snapshot
    UndoNode* snapshotNode = findNearestSnapshot(target);

    // Start from snapshot or root
    if (snapshotNode && snapshotNode->snapshot && onRestore_) {
        onRestore_(*snapshotNode->snapshot);
    } else {
        // No snapshot found - clear and replay from root
        if (onClear_) {
            onClear_();
        }
        snapshotNode = root_;
    }

    // Build path from snapshot to target
    std::vector<UndoNode*> path;
    UndoNode* node = target;
    while (node != snapshotNode && node != nullptr) {
        path.push_back(node);
        node = node->parent;
    }

    // Replay strokes in order (path is in reverse)
    std::reverse(path.begin(), path.end());
    for (UndoNode* n : path) {
        if (onApplyStroke_ && !n->stroke.isEmpty()) {
            onApplyStroke_(n->stroke);
        }
    }
}

UndoNode* UndoTree::findNearestSnapshot(UndoNode* node) const {
    while (node) {
        if (node->snapshot) {
            return node;
        }
        node = node->parent;
    }
    return nullptr;
}

// =============================================================================
// Tree Traversal
// =============================================================================

std::vector<UndoNode*> UndoTree::getAllNodes() const {
    std::vector<UndoNode*> result;
    if (root_) {
        collectAllNodes(root_, result);
    }
    return result;
}

void UndoTree::collectAllNodes(UndoNode* node, std::vector<UndoNode*>& result) const {
    if (!node) return;
    result.push_back(node);
    for (UndoNode* child : node->children) {
        collectAllNodes(child, result);
    }
}

std::vector<UndoNode*> UndoTree::getPathToNode(UndoNode* node) const {
    std::vector<UndoNode*> path;
    while (node) {
        path.push_back(node);
        node = node->parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

UndoNode* UndoTree::findNode(uint64_t nodeId) const {
    return findNodeRecursive(root_, nodeId);
}

UndoNode* UndoTree::findNodeRecursive(UndoNode* node, uint64_t nodeId) const {
    if (!node) return nullptr;
    if (node->id == nodeId) return node;

    for (UndoNode* child : node->children) {
        UndoNode* found = findNodeRecursive(child, nodeId);
        if (found) return found;
    }
    return nullptr;
}

// =============================================================================
// Memory Management
// =============================================================================

void UndoTree::trimOldestBranch() {
    if (totalNodes_ <= 1) return;

    // Find oldest leaf that isn't on the path to current
    std::vector<UndoNode*> currentPath = getPathToNode(current_);

    // BFS to find oldest leaf not on current path
    std::vector<UndoNode*> leaves;
    std::vector<UndoNode*> queue = {root_};

    while (!queue.empty()) {
        UndoNode* node = queue.front();
        queue.erase(queue.begin());

        if (node->children.empty()) {
            // It's a leaf - check if it's not on current path
            if (std::find(currentPath.begin(), currentPath.end(), node) == currentPath.end()) {
                leaves.push_back(node);
            }
        } else {
            for (UndoNode* child : node->children) {
                queue.push_back(child);
            }
        }
    }

    if (leaves.empty()) return;

    // Sort by timestamp (oldest first)
    std::sort(leaves.begin(), leaves.end(), [](UndoNode* a, UndoNode* b) {
        return a->timestamp < b->timestamp;
    });

    // Delete oldest leaf
    UndoNode* toDelete = leaves.front();
    if (toDelete->parent) {
        auto& siblings = toDelete->parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), toDelete), siblings.end());

        // Fix active child index if needed
        if (toDelete->parent->activeChildIndex >= static_cast<int>(siblings.size())) {
            toDelete->parent->activeChildIndex = std::max(0, static_cast<int>(siblings.size()) - 1);
        }
    }

    delete toDelete;
    totalNodes_--;

    std::cout << "[UndoTree] Trimmed oldest node, total now " << totalNodes_ << std::endl;
}

void UndoTree::deleteSubtree(UndoNode* node) {
    if (!node) return;
    for (UndoNode* child : node->children) {
        deleteSubtree(child);
    }
    delete node;
    totalNodes_--;
}

size_t UndoTree::getMemoryUsage() const {
    size_t total = 0;
    std::vector<UndoNode*> nodes = getAllNodes();

    for (UndoNode* node : nodes) {
        // Node struct
        total += sizeof(UndoNode);

        // Stroke data
        total += node->stroke.points.size() * sizeof(StrokePoint);

        // Snapshot
        if (node->snapshot) {
            total += node->snapshot->byteSize();
        }
    }

    return total;
}

void UndoTree::clear() {
    if (root_) {
        deleteSubtree(root_);
        root_ = nullptr;
        current_ = nullptr;
        totalNodes_ = 0;
    }

    // Recreate root
    root_ = new UndoNode();
    root_->id = nextNodeId_++;
    current_ = root_;
    totalNodes_ = 1;
}

} // namespace undo_tree
