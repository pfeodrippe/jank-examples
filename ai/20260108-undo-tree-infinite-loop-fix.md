# Undo Tree Infinite Loop Fix

## Date: 2026-01-08

## What I Learned

### The Bug
The app was crashing (hanging in an infinite loop) after drawing 10 strokes. The last log message was:
```
[UndoTree] Stored snapshot at depth 10
```

### Root Cause
In `undo_tree.cpp`, the `trimOldestBranch()` function had a bug when dealing with **linear trees** (no branches):

1. `maxNodes` was set to 10, `snapshotInterval` to 1
2. At depth 10, after storing snapshot, `totalNodes` becomes 11
3. The `while (totalNodes_ > maxNodes_)` loop triggers trimming
4. `trimOldestBranch()` looks for leaves NOT on the current path
5. **In a linear tree, ALL nodes are on the current path**
6. `leaves.empty()` is true, function returns without doing anything
7. **But the while loop condition is still true** - infinite loop!

### The Fix
Added handling for linear trees in `trimOldestBranch()`:

```cpp
// If no leaves off the current path, trim oldest from the path itself
// (but never trim root or current node)
if (leaves.empty()) {
    // For linear trees, trim the node right after root
    // This loses the oldest undo history but keeps the tree from growing unbounded
    if (currentPath.size() > 2) {  // Need root + at least 2 nodes
        UndoNode* toTrim = currentPath[1];  // First node after root

        // Move its children to root and update root's children
        root_->children.clear();
        if (!toTrim->children.empty()) {
            for (UndoNode* child : toTrim->children) {
                child->parent = root_;
                root_->children.push_back(child);
            }
            root_->activeChildIndex = 0;
        }

        delete toTrim;
        totalNodes_--;

        std::cout << "[UndoTree] Trimmed oldest path node, total now " << totalNodes_ << std::endl;
    }
    return;
}
```

### Key Insight
The undo tree is designed like Emacs's undo-tree, preserving all branches. But when there are no branches (common case of linear drawing), the original code couldn't trim anything from the current path, leading to unbounded growth or infinite loops.

The fix "promotes" the children of the oldest node (first after root) to be direct children of root, effectively removing the oldest undo history while keeping the tree functional.

## Files Modified

- `src/vybe/app/drawing/native/undo_tree.cpp` - Added linear tree handling in `trimOldestBranch()`
- `src/vybe/app/drawing/native/metal_renderer.mm` - Removed `#include "undo_tree.cpp"` (was causing duplicate symbols)
- `DrawingMobile/config-common.yml` - Changed source paths to use originals instead of jank-resources copies:
  - `../src/vybe/app/drawing/native/metal_renderer.mm`
  - `../src/vybe/app/drawing/native/undo_tree.cpp`
  - `../src/vybe/app/drawing/native/stamp_shaders.metal`
  - Added header search path for native drawing code

## Additional Fix: Duplicate Symbols

When compiling `undo_tree.cpp` as a separate translation unit, we got "20 duplicate symbols" errors because `metal_renderer.mm` was also including `undo_tree.cpp` directly for "single-TU build". Removed the include since we now compile separately.

## Commands I Ran

```bash
make drawing-ios-jit-sim-run
```

## What's Next

- Test the fix by drawing more than 10 strokes on the iOS device
- Verify undo/redo still works correctly after trimming
- The jank-resources folder copies are now obsolete for native C++ files (only needed for jank source files bundled for JIT runtime)
