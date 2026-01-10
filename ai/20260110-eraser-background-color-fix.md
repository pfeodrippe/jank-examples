# Eraser Background Color Fix

**Status: FIXED**

## Problem
The eraser was painting with pure white (1.0, 1.0, 1.0) instead of the actual canvas background color (0.95, 0.95, 0.92), making erased areas appear whiter than the surrounding canvas - especially visible on frames other than frame 0.

## Root Cause
Multiple places in `drawing_mobile_ios.mm` had hardcoded white/off-white colors that didn't use a shared constant:
- Eraser mode used pure white `(1.0, 1.0, 1.0)`
- Canvas initialization and frame switching used various values

The jank weave defines `:bg-color [0.95 0.95 0.92 1.0]` but this wasn't being used consistently.

## Fix
1. Added paper background color constants matching the jank weave:
```cpp
// Paper background color (off-white, matches weave :bg-color [0.95 0.95 0.92 1.0])
static const float PAPER_BG_R = 0.95f;
static const float PAPER_BG_G = 0.95f;
static const float PAPER_BG_B = 0.92f;
static const float PAPER_BG_A = 1.0f;
```

2. Updated all places to use these constants:
   - Initial canvas clear (line ~1903)
   - Eraser mode - finger touch (line ~2114)
   - Eraser mode - pen touch (line ~2672)
   - FrameStore empty frame clear (line ~1317)
   - Finger swipe frame navigation - next/prev (lines ~2473, ~2479)
   - Pen swipe frame navigation - next/prev (lines ~2511, ~2518)

## Files Modified
- `/Users/pfeodrippe/dev/something/DrawingMobile/drawing_mobile_ios.mm`
  - Lines 344-348: Added `PAPER_BG_R/G/B/A` constants
  - All `metal_stamp_clear_canvas()` and `metal_stamp_set_brush_color()` calls now use these constants

## Key Learning
**Never hardcode colors in multiple places** - always define constants for shared values like background colors. This ensures consistency and makes future changes easier.

The jank code defines colors in `core.jank`:
```clojure
:bg-color [0.95 0.95 0.92 1.0]  ; Off-white paper
```

The C++ code should mirror these values using constants.

## Commands
```bash
make drawing-ios-jit-sim-run
```

## Remaining Issue
The frame wheel UI and REPL frame switching still use separate systems:
- **FrameStore** (wheel): Stores pixel snapshots in GPU cache
- **AnimThread** (REPL): Stores stroke data and re-renders

These should be unified so frame changes from either source update both systems.
