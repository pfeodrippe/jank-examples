# Undo Tree Implementation Plan

**Date**: 2025-01-05

## Overview

Implement an Emacs-style undo tree for the drawing app, allowing:
- **Branching history**: Never lose any state
- **Non-linear navigation**: Jump to any point in history
- **Visual tree display**: See your edit history graphically

## Research Sources

- [Emacs undo-tree package](https://www.dr-qubit.org/undo-tree.html) - Core concepts
- [Construct an Undo Tree From a Linear Undo History](https://archive.casouri.cc/note/2021/visual-undo-tree/index.html) - Algorithm details
- [Vim undo branches](https://vim.fandom.com/wiki/Using_undo_branches) - Similar implementation
- [Procreate Dreams undo history](https://help.procreate.com/articles/dxxgnk-managing-undo-history) - Drawing app inspiration (stores history in file!)
- [Command Pattern for Undo/Redo](https://medium.com/bugless/undo-redo-graphics-editor-with-command-pattern-in-kotlin-5354676c2166) - Graphics editor approach

## Core Concepts

### Linear Undo (Traditional)
```
A → B → C → D
        ↑
      (undo to C, make change E)

A → B → C → E  (D is LOST!)
```

### Undo Tree (Our Implementation)
```
A → B → C → D  (branch 1)
        ↓
        E → F  (branch 2, D is preserved!)
```

## Data Structures

### 1. UndoNode - Single point in history
```cpp
struct UndoNode {
    uint64_t id;                      // Unique node ID
    uint64_t timestamp;               // When this action occurred
    UndoNode* parent;                 // Parent node (previous state)
    std::vector<UndoNode*> children;  // Child nodes (branches)
    int activeChildIndex;             // Which child is "current" branch

    // The action that created this state
    StrokeData stroke;                // Stroke points, color, brush settings

    // Optional: Canvas snapshot for fast navigation
    // (store every N nodes to balance memory vs speed)
    std::shared_ptr<CanvasSnapshot> snapshot;
};
```

### 2. StrokeData - The action/command
```cpp
struct StrokeData {
    std::vector<StrokePoint> points;  // Position, pressure, timestamp
    BrushSettings brush;              // Size, opacity, color, type
    uint64_t layerId;                 // Which layer (future feature)
};

struct StrokePoint {
    float x, y;           // Canvas coordinates
    float pressure;       // 0.0 - 1.0
    uint64_t timestamp;   // For animation replay!
};
```

### 3. UndoTree - The complete history
```cpp
class UndoTree {
    UndoNode* root;           // Initial empty canvas
    UndoNode* current;        // Where we are now
    uint64_t nextNodeId;      // ID counter

    // Settings
    int maxNodes;             // Limit history size (e.g., 250 like Procreate)
    int snapshotInterval;     // Store snapshot every N nodes

public:
    // Core operations
    void recordStroke(const StrokeData& stroke);
    void undo();
    void redo();                        // Go to active child
    void redoBranch(int branchIndex);   // Go to specific branch
    void jumpToNode(uint64_t nodeId);   // Navigate anywhere

    // Tree info
    UndoNode* getRoot();
    UndoNode* getCurrent();
    std::vector<UndoNode*> getAllNodes();  // For visualization

    // Persistence
    void saveToFile(const std::string& path);
    void loadFromFile(const std::string& path);
};
```

## Algorithm Details

### Recording a New Stroke
```cpp
void UndoTree::recordStroke(const StrokeData& stroke) {
    // Create new node
    UndoNode* newNode = new UndoNode();
    newNode->id = nextNodeId++;
    newNode->timestamp = getCurrentTime();
    newNode->stroke = stroke;
    newNode->parent = current;

    // Link to parent
    current->children.push_back(newNode);
    current->activeChildIndex = current->children.size() - 1;

    // Move current to new node
    current = newNode;

    // Maybe store snapshot
    if (getDepth(current) % snapshotInterval == 0) {
        current->snapshot = captureCanvas();
    }

    // Trim old history if needed
    trimIfNeeded();
}
```

### Undo Operation
```cpp
void UndoTree::undo() {
    if (current->parent == nullptr) return;  // At root

    // Move to parent
    current = current->parent;

    // Restore canvas state
    restoreToNode(current);
}
```

### Redo Operation (with branching)
```cpp
void UndoTree::redo() {
    if (current->children.empty()) return;  // No children

    // Go to active child branch
    current = current->children[current->activeChildIndex];

    // Apply the stroke
    applyStroke(current->stroke);
}

void UndoTree::redoBranch(int branchIndex) {
    if (branchIndex >= current->children.size()) return;

    // Switch active branch
    current->activeChildIndex = branchIndex;

    // Then redo
    redo();
}
```

### Restore to Any Node (Fast Path)
```cpp
void UndoTree::restoreToNode(UndoNode* target) {
    // Find nearest ancestor with snapshot
    UndoNode* snapshotNode = findNearestSnapshot(target);

    // Restore from snapshot
    if (snapshotNode->snapshot) {
        restoreFromSnapshot(snapshotNode->snapshot);
    } else {
        // No snapshot - replay from root
        clearCanvas();
        snapshotNode = root;
    }

    // Replay strokes from snapshot to target
    std::vector<UndoNode*> path = getPath(snapshotNode, target);
    for (UndoNode* node : path) {
        applyStroke(node->stroke);
    }

    current = target;
}
```

## Canvas State Management

### Option A: Full Snapshots (Simple, Memory Heavy)
- Store complete canvas texture for each node
- ~4MB per snapshot at 1024x1024 RGBA
- 250 undos = 1GB memory (too much!)

### Option B: Commands Only (Memory Light, Slow Navigation)
- Store only stroke data
- Replay all strokes to reach any state
- Slow for deep navigation

### Option C: Hybrid (Recommended)
- Store snapshot every N strokes (e.g., every 25)
- Store stroke commands between snapshots
- Max replay = N strokes
- Memory = (250/25) * 4MB = 40MB (reasonable)

## Visual Tree Display (Future Enhancement)

```
        ┌─[5]─[6]─[7]    ← Branch 1 (older)
    [1]─[2]─[3]─[4]
             └─[8]─[9]●  ← Branch 2 (current)

● = current position
```

- Show as overlay panel
- Tap node to jump
- Current branch highlighted
- Timestamps shown on hover

## Gesture Integration

| Gesture | Action |
|---------|--------|
| Two-finger tap | Undo |
| Three-finger tap | Redo (active branch) |
| Three-finger swipe left | Undo multiple |
| Three-finger swipe right | Redo multiple |
| Hold + tap branch | Switch to different branch |

## File Format (for persistence)

```
UNDO_TREE_V1
{
  "version": 1,
  "maxNodes": 250,
  "snapshotInterval": 25,
  "currentNodeId": 42,
  "nodes": [
    {
      "id": 0,
      "parentId": null,
      "timestamp": 1704412800000,
      "stroke": null,  // Root has no stroke
      "hasSnapshot": true
    },
    {
      "id": 1,
      "parentId": 0,
      "timestamp": 1704412801234,
      "stroke": {
        "points": [[100, 200, 0.8], [102, 202, 0.85], ...],
        "brush": {"size": 20, "opacity": 1.0, "color": [0, 0, 1, 1]}
      },
      "hasSnapshot": false
    },
    ...
  ],
  "snapshots": {
    "0": "base64_encoded_png...",
    "25": "base64_encoded_png...",
    ...
  }
}
```

## Implementation Steps

### Phase 1: Core Data Structures
1. Create `UndoNode`, `StrokeData`, `UndoTree` structs/classes
2. Implement basic tree operations (add, undo, redo)
3. Store strokes in nodes during drawing

### Phase 2: Canvas Restoration
1. Implement canvas snapshot capture
2. Implement snapshot restoration
3. Implement stroke replay
4. Add hybrid snapshot strategy

### Phase 3: Branching
1. Track multiple children per node
2. Implement branch switching
3. Add "active branch" tracking

### Phase 4: Gestures
1. Two-finger tap for undo
2. Three-finger tap for redo
3. Rapid undo/redo on hold

### Phase 5: Persistence (Optional)
1. Serialize tree to JSON/binary
2. Save with drawing file
3. Load on file open

### Phase 6: Visual Tree (Optional)
1. Render tree as overlay
2. Add tap-to-navigate
3. Show current position

## Memory Considerations

- **Stroke data**: ~100-500 bytes per stroke
- **Snapshot**: ~4MB at 1024x1024 RGBA
- **With hybrid (25 stroke interval)**:
  - 250 strokes = 10 snapshots = 40MB
  - 250 strokes * 300 bytes = 75KB
  - Total: ~40MB (acceptable)

## Performance Considerations

- **Recording stroke**: O(1) - just add node
- **Undo/Redo by 1**: O(1) - move pointer
- **Jump to node**: O(N) worst case, O(snapshot_interval) average
- **Snapshot capture**: ~10ms (async to avoid jank)

## Code Location

New files to create:
- `src/vybe/app/drawing/native/undo_tree.hpp` - Data structures
- `src/vybe/app/drawing/native/undo_tree.cpp` - Implementation

Modify:
- `DrawingMobile/drawing_mobile_ios.mm` - Gesture handling
- `src/vybe/app/drawing/native/metal_renderer.mm` - Snapshot capture

## Next Steps After Implementation

1. **Animation Recording**: Store timestamps with stroke points for playback
2. **Layer Support**: Track which layer each stroke belongs to
3. **Collaborative Undo**: Branch per user in multiplayer
