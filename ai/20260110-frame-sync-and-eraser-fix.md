# Frame Sync and Eraser Background Color Fix

**Date:** 2026-01-10

## What I Learned

### 1. Eraser Background Color Issue
The eraser was painting with pure white (1.0, 1.0, 1.0) instead of the canvas background color (0.95, 0.95, 0.92). Multiple places had hardcoded color values.

**Fix:** Added shared constants for paper background color:
```cpp
// Paper background color (off-white, matches weave :bg-color [0.95 0.95 0.92 1.0])
static const float PAPER_BG_R = 0.95f;
static const float PAPER_BG_G = 0.95f;
static const float PAPER_BG_B = 0.92f;
static const float PAPER_BG_A = 1.0f;
```

### 2. Frame Sync Between Wheel UI and AnimThread
Two separate frame systems existed:
- **FrameStore (wheel UI)**: Uses pixel snapshots in GPU cache, instant switching
- **AnimThread (REPL/jank)**: Uses stroke data, re-renders from strokes

**Solution:** Added callback mechanism to sync both directions:

1. **Wheel -> AnimThread**: In `framestore_load_frame()`, call `anim_goto_frame(index)`
2. **AnimThread -> Wheel**: Register callback `onAnimFrameChange()` that updates wheel rotation
3. **Guard variable**: `g_syncingFrame` prevents infinite recursion

### 3. C++ Namespace Issues
When defining a global variable inside a namespace and using it outside, you must qualify it:
```cpp
namespace animation {
    static FrameChangeCallback g_frameChangeCallback = nullptr;
}

// Outside namespace - MUST use animation:: prefix
void anim_set_frame_change_callback(FrameChangeCallback callback) {
    animation::g_frameChangeCallback = callback;  // NOT just g_frameChangeCallback
}
```

### 4. Forward Declaration Order
Variables and structs must be defined BEFORE they're used. Move declarations earlier if you get "undeclared identifier" errors.

### 5. jank Header Requires
To use C functions in jank, add the header to the namespace require:
```clojure
(ns vybe.app.drawing.metal
  (:require ["vybe/app/drawing/native/animation_thread.h" :as anim :scope ""]))
```

Then create wrapper functions:
```clojure
(defn anim-next! [] (anim/anim_next_frame))
```

## Files Modified

- `DrawingMobile/drawing_mobile_ios.mm`
  - Added PAPER_BG_R/G/B/A constants
  - Added g_syncingFrame guard variable
  - Added g_frameWheelPtr for callback access
  - Added onAnimFrameChange() callback
  - Updated framestore_load_frame() to sync with AnimThread
  - Registered callback with anim_set_frame_change_callback()

- `src/vybe/app/drawing/native/animation_thread.h`
  - Added FrameChangeCallback typedef
  - Added anim_set_frame_change_callback() declaration

- `src/vybe/app/drawing/native/animation_thread.mm`
  - Added g_frameChangeCallback static variable in animation namespace
  - Implemented anim_set_frame_change_callback()
  - Added callback invocation in anim_next_frame(), anim_prev_frame(), anim_goto_frame()

- `src/vybe/app/drawing/metal.jank`
  - Added animation_thread.h require
  - Added wrapper functions: anim-current-frame, anim-next!, anim-prev!, anim-goto!

## Commands

```bash
# Build and run iOS simulator
make drawing-ios-jit-sim-run

# Test frame navigation via nREPL
clj-nrepl-eval -p 5580 "(in-ns 'vybe.app.drawing.metal) (anim-current-frame)"
clj-nrepl-eval -p 5580 "(do (anim-next!) (anim-current-frame))"
clj-nrepl-eval -p 5580 "(do (anim-goto! 0) (anim-current-frame))"
```

## Key Learnings

1. **Never hardcode colors** - use constants for shared values
2. **Guard variables** prevent infinite recursion in bidirectional sync
3. **Namespace qualification** required for globals defined in namespaces
4. **Declaration order matters** in C++ - define before use
5. **jank requires :reload** to pick up namespace changes
