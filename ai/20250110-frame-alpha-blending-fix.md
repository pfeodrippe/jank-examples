# Frame Background Color Fix (Alpha Blending)

**Status: FIXED**

## Problem
When switching between animation frames, frame 0 appeared whiter (correct 0.95, 0.95, 0.92 background) while other frames appeared darker.

## Root Cause
The stamp rendering pipeline used alpha blending for BOTH RGB and alpha channels:

```cpp
// BEFORE (BROKEN):
sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
```

This caused the canvas's alpha channel to decrease when strokes were drawn:
- Canvas starts at alpha = 1.0
- Draw stroke with alpha = 0.5
- Result alpha = 0.5 * 0.5 + 1.0 * 0.5 = 0.75 (REDUCED!)

When the canvas was blitted to the gray (0.3) screen background, the reduced alpha made the canvas appear darker:
```
final_rgb = canvas_rgb * canvas_alpha + 0.3 * (1 - canvas_alpha)
```

Frame 0 appeared correct because it started fresh with no strokes (alpha = 1.0).

## Fix
Preserve destination alpha at 1.0 by using different blend factors for alpha:

```cpp
// AFTER (FIXED):
sourceAlphaBlendFactor = MTLBlendFactorZero;      // Don't add source alpha
destinationAlphaBlendFactor = MTLBlendFactorOne;  // Keep dest alpha = 1.0
```

This keeps canvas alpha at 1.0 regardless of stroke alpha, while RGB blending works correctly.

## File Modified
- `/Users/pfeodrippe/dev/something/src/vybe/app/drawing/native/metal_renderer.mm`
  - Lines 529, 531: Changed alpha blend factors in stamp pipeline
  - Other brush pipelines (crayon, watercolor, marker) inherit the fix via shared pipelineDesc

## Key Learning
When rendering strokes to an off-screen canvas that will be composited later:
- RGB blending: standard `src * src.a + dst * (1 - src.a)`
- Alpha blending: preserve destination alpha with `src * 0 + dst * 1`

This prevents alpha channel "erosion" that causes unexpected darkening when the canvas is blitted to a colored background.

## Commands
```bash
make drawing-ios-jit-sim-run
clj-nrepl-eval -p 5580 "(require '[vybe.app.drawing.state :as state] :reload)"
clj-nrepl-eval -p 5580 "(state/anim-next-frame!)"  ; Switch frames to test
clj-nrepl-eval -p 5580 "(state/anim-prev-frame!)"
```
