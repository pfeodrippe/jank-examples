# Undo Visual Corruption Analysis & Fix

## Problem
When using undo, the painting appears:
1. **Whiter** than before
2. **Small noises** (visual artifacts)

## Root Cause Analysis

### Discovery Process

1. Examined `undo_tree.cpp` - the `restoreToNode()` function:
   - Finds nearest snapshot ancestor
   - Restores from snapshot (or clears canvas if no snapshot)
   - **Replays strokes** from snapshot to target node

2. Examined stroke replay in `metal_renderer.mm`:
   - `setApplyStrokeCallback` replays recorded strokes
   - Calls `begin_stroke()`, `add_stroke_point()` loop, `end_stroke()`

3. **Found the bug** in `interpolateFrom:to:` - uses `rand()` for jitter/scatter:
   ```cpp
   // PROBLEMATIC: rand() is non-deterministic!
   float scatterAmount = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * scatter;
   float jitter = 1.0f + ((float)rand() / RAND_MAX - 0.5f) * 2.0f * sizeJitter;
   ```

### The Bug

**`rand()` produces DIFFERENT values during replay!**

- Original stroke: Random sequence A produces positions P1, P2, P3...
- Replayed stroke: Random sequence B (different!) produces Q1, Q2, Q3...

This explains both symptoms:
1. **Whiter**: Different opacity jitter values = different alpha accumulation
2. **Small noises**: Different scatter/size values = stamps in slightly different positions

## Solution: Fully Deterministic PRNG

**Key insight**: Don't use `rand()` at all! Create a deterministic hash function that:
- Takes a seed + counter as input
- Always produces the same output for the same input
- No global state to worry about

### Implementation

#### 1. Add deterministic hash function (`metal_renderer.mm`)

```cpp
// Deterministic hash function based on seed and counter
// Returns a float in range [0, 1)
inline float deterministic_random(uint32_t seed, uint32_t counter) {
    uint32_t h = seed;
    h ^= counter;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return (float)(h & 0x00FFFFFF) / (float)0x01000000;
}
```

#### 2. Add stroke random state to impl class

```objc
@property (nonatomic, assign) uint32_t strokeRandomSeed;
@property (nonatomic, assign) uint32_t strokeRandomCounter;
```

#### 3. Replace rand() calls in interpolation

```cpp
// Apply scatter (deterministic)
if (scatter > 0.0f) {
    float r = deterministic_random(self.strokeRandomSeed, self.strokeRandomCounter++);
    float scatterAmount = (r - 0.5f) * 2.0f * scatter;
    // ...
}
```

#### 4. Add seed to StrokeData (`undo_tree.hpp`)

```cpp
struct StrokeData {
    // ...
    uint32_t randomSeed;  // For deterministic replay
};
```

#### 5. Set seed at stroke begin and replay

```cpp
// At stroke begin:
uint32_t strokeSeed = ticks ^ ((uint32_t)(x * 1000) << 16) ^ ((uint32_t)(y * 1000));
g_metal_renderer->set_stroke_random_seed(strokeSeed);
g_current_stroke.randomSeed = strokeSeed;

// During replay:
g_metal_renderer->set_stroke_random_seed(stroke.randomSeed);
g_metal_renderer->begin_stroke(first.x, first.y, first.pressure);
```

## Files Modified

1. `src/vybe/app/drawing/native/undo_tree.hpp` - Add `randomSeed` field
2. `src/vybe/app/drawing/native/metal_renderer.h` - Add `set_stroke_random_seed()` method
3. `src/vybe/app/drawing/native/metal_renderer.mm`:
   - Add `deterministic_random()` hash function
   - Add `strokeRandomSeed` and `strokeRandomCounter` properties
   - Replace ALL `rand()` calls with `deterministic_random()`
   - Add `set_stroke_random_seed()` implementation
   - Update `undo_begin_stroke()` to set seed
   - Update replay callback to set seed before replay

## Why This Solution Is Better

| Approach | Problem |
|----------|---------|
| `srand(seed)` before stroke | Global state, other code might call `rand()` |
| **Deterministic hash** | No global state, always reproducible |

## Additional Root Causes Discovered

### Bug #2: Double-Rendering During Live Drawing

**Problem**: During live drawing, `render_current_stroke()` is called every frame, rendering ALL points in `_points`. So:
- Frame 1: renders point 1
- Frame 2: renders points 1,2 (point 1 again!)
- Frame 3: renders points 1,2,3 (points 1,2 again!)

But during replay, `end_stroke()` calls `renderPointsWithHardness` only ONCE.

With alpha blending, more renders = darker. So original appears darker, replay appears "washed out".

**Fix**: Track `renderedPointCount` to only render NEW points:

```cpp
// In renderPointsWithHardness:
size_t newPointCount = _points.size() - self.renderedPointCount;
if (newPointCount == 0) return;

// Draw only NEW points
[encoder drawPrimitives:MTLPrimitiveTypePoint
            vertexStart:self.renderedPointCount
            vertexCount:newPointCount];

// Update for next render
self.renderedPointCount = _points.size();
```

Reset `renderedPointCount = 0` in:
- `begin_stroke()` - start of new stroke
- `commitStrokeToCanvas` - stroke committed
- Initialization

### Bug #3: Pivot Point Not Reset During Canvas Reset

**Problem**: During two-finger gestures, the pivot point is updated to the gesture center (where your fingers are). When resetting the view with a quick pinch, only pan/scale/rotation were being reset - the pivot stayed at the last gesture center instead of returning to screen center.

**Fix**: Reset pivotX/pivotY to screen center immediately when starting the reset animation:

```cpp
// CRITICAL: Reset pivot to screen center immediately
// (pivot changes during gestures but must return to center for correct default view)
canvasTransform.pivotX = width / 2.0f;
canvasTransform.pivotY = height / 2.0f;
```

### Bug #4: Brush Type Not Applied During Replay

**Problem**: `set_brush()` only copied settings to `brush_` but did NOT update:
- `impl_.currentBrushType` - used to select pipeline (crayon, watercolor, etc.)
- `impl_.currentShapeTexture` - used for shape texture
- `impl_.currentGrainTexture` - used for grain texture
- `impl_.shapeInverted` - used for shape inversion

So replayed strokes used the CURRENT brush type/textures instead of the recorded ones!

**Fix**: Update `set_brush()` to also set impl_ properties:

```cpp
void MetalStampRenderer::set_brush(const BrushSettings& settings) {
    brush_ = settings;

    // CRITICAL: Also update impl_ properties used during rendering
    if (is_ready()) {
        impl_.currentBrushType = static_cast<int>(settings.type);
        impl_.currentShapeTexture = [impl_ getTextureById:settings.shape_texture_id];
        impl_.currentGrainTexture = [impl_ getTextureById:settings.grain_texture_id];
        impl_.shapeInverted = settings.shape_inverted;
    }
}
```

## All Files Modified

1. `src/vybe/app/drawing/native/undo_tree.hpp`:
   - Add `randomSeed` field to `StrokeData`
   - Add `shape_inverted` field to `BrushSettings`

2. `src/vybe/app/drawing/native/metal_renderer.h` - Add `set_stroke_random_seed()` method

3. `src/vybe/app/drawing/native/metal_renderer.mm`:
   - Add `deterministic_random()` hash function
   - Add `strokeRandomSeed`, `strokeRandomCounter`, `renderedPointCount` properties
   - Replace ALL `rand()` calls with `deterministic_random()`
   - Add `set_stroke_random_seed()` implementation
   - Update `undo_begin_stroke()` to set seed and save `shape_inverted`
   - Update replay callback to set seed and restore `shape_inverted`
   - Track `renderedPointCount` in `renderPointsWithHardness` - only render NEW points
   - Reset `renderedPointCount` in `begin_stroke`, `commitStrokeToCanvas`, init
   - Update `set_brush()` to set `impl_.currentBrushType`, textures, `shapeInverted`

## Summary of Six Fixes

| Bug | Symptom | Root Cause | Fix |
|-----|---------|------------|-----|
| Non-deterministic random | Small noises, different positions | `rand()` different values | Deterministic hash function |
| Double-rendering | Washed out replay | Points rendered many times live, once replay | `renderedPointCount` tracking |
| Brush not applied | Wrong brush type in replay | `set_brush()` didn't update impl_ | Update impl_ properties |
| Missing `shape_inverted` | Brush appearance changes | Field not saved/restored in undo | Added to BrushSettings, save/restore |
| Undo not persisted after frame switch | Undo lost when switching frames | Frame cache not updated after undo | Call `framestore_save_current_fast()` after undo/redo |
| Pinch reset goes to wrong position | Canvas not centered after reset | Pivot point not reset to screen center | Reset pivotX/pivotY when starting reset animation |

## Testing

1. Draw strokes with crayon brush (has jitter/scatter)
2. Undo - should look identical to previous state
3. Redo - should look identical to after drawing
4. Multiple undo/redo cycles - no degradation
5. **NEW**: Change brush type, undo - should replay with ORIGINAL brush
6. **NEW**: Draw with different brushes, undo multiple times - each stroke should use its recorded brush
7. **NEW**: Quick pinch to reset canvas - should return to exact initial position (centered)
8. **NEW**: Pan/zoom canvas, then quick pinch - canvas should snap back to centered view identical to startup
