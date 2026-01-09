# Palm Rejection Deep Analysis - ULTRATHINK

## Date: 2026-01-08

## The Problem

When drawing with Apple Pencil while a finger is touching the screen, the canvas moves/pans with the finger instead of staying still. The pencil drawing is affected by finger position.

## Root Cause Analysis

### Current Palm Rejection (Incomplete)

The current fix only blocks NEW finger touches during pencil drawing:

```cpp
// In FINGER_DOWN handler (line 1878)
if (pencil_detected && is_drawing) {
    // Ignore finger touches while drawing with pencil
}
```

**THE BUG**: This only prevents NEW fingers from starting gestures. It does NOT:
1. Cancel EXISTING gesture state when pencil starts drawing
2. Block FINGER_MOTION events from updating canvas transform
3. Reset finger tracking state (`hasFinger0`, `gesture.isActive`)

### The Race Condition

Timeline that causes the bug:
1. User's palm touches screen → `hasFinger0 = true`, `pendingFinger0_id` set
2. User touches pencil to screen → `is_drawing = true`
3. User moves palm slightly → `FINGER_MOTION` fires
4. Palm rejection check in `FINGER_DOWN` is bypassed (no new DOWN event)
5. `FINGER_MOTION` handler sees `hasFinger0` is true, updates finger position
6. Canvas transform affected!

### Where Finger Motion Affects Canvas

In `SDL_EVENT_FINGER_MOTION` handler (lines 1936-1979):

```cpp
case SDL_EVENT_FINGER_MOTION: {
    // ...
    // Handle two-finger gesture motion
    else if (gesture.isActive) {
        // Update finger positions
        // Update canvas transform from gesture  <-- THIS MOVES CANVAS!
        updateTransformFromGesture(canvasTransform, gesture, width, height);
    }
    // Handle single-finger drawing (for simulator testing)
    else if (is_drawing && hasFinger0 && fingerId == pendingFinger0_id) {
        // This updates finger position and affects drawing coords
    }
    //...
}
```

## The Correct Fix

### Strategy 1: Cancel finger state when pencil starts drawing

In `SDL_EVENT_PEN_DOWN`, when starting to draw:

```cpp
// Drawing with Apple Pencil - cancel any finger gesture state
if (gesture.isActive) {
    gesture.isActive = false;
    std::cout << "Gesture cancelled - pencil drawing started" << std::endl;
}
if (hasFinger0) {
    hasFinger0 = false;
    pendingFinger0_id = 0;
    std::cout << "Finger tracking cancelled - pencil drawing started" << std::endl;
}
threeFingerGesture.isActive = false;
```

### Strategy 2: Block finger motion during pencil drawing

In `SDL_EVENT_FINGER_MOTION`, add early exit:

```cpp
case SDL_EVENT_FINGER_MOTION: {
    // Palm rejection - ignore ALL finger motion while pencil is drawing
    if (pencil_detected && is_drawing) {
        break;  // Skip all finger motion processing
    }
    // ... rest of handler
}
```

### Strategy 3: Block finger up during pencil drawing

In `SDL_EVENT_FINGER_UP`, add early exit:

```cpp
case SDL_EVENT_FINGER_UP: {
    // Palm rejection - ignore finger lifts while pencil is drawing
    if (pencil_detected && is_drawing) {
        break;
    }
    // ... rest of handler
}
```

## Recommended Implementation

Apply ALL three strategies:

1. **PEN_DOWN**: Cancel existing finger state when pencil starts drawing
2. **FINGER_MOTION**: Skip all processing when pencil is drawing
3. **FINGER_UP**: Skip all processing when pencil is drawing

This ensures complete isolation of pencil drawing from finger touches.

## Files to Modify

- `DrawingMobile/drawing_mobile_ios.mm`
  - Line ~2278: Add finger state cancellation in PEN_DOWN drawing branch
  - Line ~1936: Add early exit in FINGER_MOTION
  - Add early exit in FINGER_UP handler

---

## Implementation Notes

The key insight is that palm rejection must be COMPREHENSIVE:
- Block new touches (FINGER_DOWN) ✓ (already done)
- Block motion updates (FINGER_MOTION) ← MISSING
- Block release processing (FINGER_UP) ← MISSING
- Cancel existing state (on PEN_DOWN) ← MISSING

All four aspects are needed for proper palm rejection.

---

## Implementation Complete

### Changes Made to `DrawingMobile/drawing_mobile_ios.mm`:

1. **FINGER_DOWN handler (line 1733-1736)**: Added early exit
   ```cpp
   if (pencil_detected && is_drawing) {
       break;
   }
   ```

2. **FINGER_MOTION handler (line 1937-1940)**: Added early exit
   ```cpp
   if (pencil_detected && is_drawing) {
       break;
   }
   ```

3. **FINGER_UP handler (line 2016-2019)**: Added early exit
   ```cpp
   if (pencil_detected && is_drawing) {
       break;
   }
   ```

4. **PEN_DOWN handler (lines 2282-2296)**: Cancel existing finger state when pencil starts drawing
   ```cpp
   if (gesture.isActive) {
       gesture.isActive = false;
       NSLog(@"[PalmReject] Cancelled two-finger gesture - pencil drawing started");
   }
   if (hasFinger0) {
       hasFinger0 = false;
       pendingFinger0_id = 0;
       NSLog(@"[PalmReject] Cancelled finger tracking - pencil drawing started");
   }
   if (threeFingerGesture.isActive) {
       threeFingerGesture.isActive = false;
       NSLog(@"[PalmReject] Cancelled three-finger gesture - pencil drawing started");
   }
   ```

### Key Insight

Palm rejection must be COMPREHENSIVE across all three finger events:
- **FINGER_DOWN**: Block new touches from registering
- **FINGER_MOTION**: Block touch movement from updating canvas transform
- **FINGER_UP**: Block release from triggering actions
- **PEN_DOWN**: Cancel existing finger state to prevent race conditions

The original fix only blocked FINGER_DOWN, allowing existing finger state to continue affecting the canvas.

---

## ZIP Brush Loading Fix

### Problem
Brushes from `Goodtype+Curated+Brush+Pack+from+Paperlike.zip` weren't loading - only 34 brushes total instead of 43.

### Root Cause
The manual ZIP parser in `brush_importer.mm` was breaking on files with data descriptors (when `flags & 0x08` is set, sizes are 0 in local header).

### Solution (Final - Build-Time Extraction)
Instead of runtime ZIP parsing, extract at **build time** using Xcode preBuildScript:

**config-common.yml:**
```yaml
preBuildScripts:
  - name: Extract Goodtype Brush Pack
    script: |
      cd "${PROJECT_DIR}/brushes"
      ZIP="Goodtype+Curated+Brush+Pack+from+Paperlike.zip"
      DEST="Goodtype_Extracted"
      if [ -f "$ZIP" ]; then
        rm -rf "$DEST" && mkdir -p "$DEST"
        unzip -o -q "$ZIP" -d /tmp/goodtype_tmp
        rm -rf /tmp/goodtype_tmp/__MACOSX
        find /tmp/goodtype_tmp -name "*.brushset" -exec mv {} "$DEST/" \;
        find /tmp/goodtype_tmp -name "*.brush" -exec mv {} "$DEST/" \;
        rm -rf /tmp/goodtype_tmp
      fi
```

**brush_importer.mm** - Added `loadBundledBrushFolder:` method to load from extracted folder:
```objc
+ (NSArray<NSNumber*>*)loadBundledBrushFolder:(NSString*)folderName {
    // Find folder in bundle, scan for .brushset and .brush files
    return [self loadBrushesFromDirectory:folderURL source:@"folder"];
}
```

**drawing_mobile_ios.mm:**
```objc
NSArray<NSString*>* folderNames = @[@"Goodtype_Extracted"];
for (NSString* folderName in folderNames) {
    NSArray<NSNumber*>* brushIds = [BrushImporter loadBundledBrushFolder:folderName];
    // ...
}
```

### Key Insight
Build-time extraction is simpler and more reliable than runtime ZIP parsing:
1. Uses system `unzip` command (handles all edge cases)
2. Works on both simulator and device
3. No complex ZIP parsing code needed
4. Extracted folder added to .gitignore (generated at build time)

### Result
- Before: 34 brushes (only .brushset bundles)
- After: **43 brushes** (34 + 9 from Goodtype folder)
