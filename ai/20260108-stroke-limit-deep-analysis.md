# Stroke Limit Deep Analysis - ULTRATHINK

## Date: 2026-01-08

## The Problem

Drawing stops after a certain amount of drawing within a single stroke. The user can lift the pen and start a new stroke, but continuous drawing hits a limit.

## Root Cause Analysis

### The Actual Limit

```cpp
// In metal_renderer.h
constexpr int MAX_POINTS_PER_STROKE = 10000;

// In interpolateFrom:to: (line 847)
for (int i = 0; i < numPoints && _points.size() < metal_stamp::MAX_POINTS_PER_STROKE; i++) {
```

When `_points.size() >= 10000`, the loop stops adding points. **New points are silently dropped.**

### Why My Previous Fix Failed

I added this check in `add_stroke_point()`:
```cpp
if (impl_.pointCount > 0 && (impl_.pointCount % 8000) == 0) {
    // flush...
}
```

**THE BUG**: `impl_.pointCount` counts RAW input points (calls to add_stroke_point).
But `_points.size()` counts INTERPOLATED points!

### The Math

With brush spacing of 0.1 (10% of brush size):
- Each raw input point generates ~10 interpolated points
- `impl_.pointCount = 800` → `_points.size() ≈ 8000`
- `impl_.pointCount = 1000` → `_points.size() ≈ 10000` (LIMIT HIT!)

My check at 8000 raw points would trigger at `_points.size() ≈ 80000` - way past the limit!

### Why the Limit Exists

The Metal point buffer is pre-allocated:
```cpp
self.pointBuffer = [self.device newBufferWithLength:sizeof(MSLPoint) * MAX_POINTS_PER_STROKE ...];
```

Writing more than 10000 points would overflow the buffer → crash.

## The Correct Fix

The flush logic must be in `interpolateFrom:to:` where `_points` is accessible:

```objc
- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to ... {
    // ... calculate numPoints ...

    for (int i = 0; i < numPoints; i++) {
        // Check if we need to flush BEFORE adding
        if (_points.size() >= metal_stamp::MAX_POINTS_PER_STROKE - 100) {
            // Flush current points to canvas
            [self renderPointsWithHardness:... ];
            [self commitStrokeToCanvas];  // This clears _points
            NSLog(@"[Stroke] Auto-flushed %zu points", _points.size());
        }

        // Now safe to add point
        _points.push_back(point);
    }
}
```

### Alternative: Expose _points.size()

Add a property to expose the interpolated point count:
```objc
@property (nonatomic, readonly) NSUInteger interpolatedPointCount;
- (NSUInteger)interpolatedPointCount { return _points.size(); }
```

Then check from C++ side:
```cpp
if (impl_.interpolatedPointCount > 8000) {
    // flush...
}
```

## Why This Is Complex

The flush needs access to brush settings for rendering:
- `hardness`, `opacity`, `flow`, `grainScale`
- `useShapeTexture`, `useGrainTexture`

These are stored in the C++ `MetalStampRenderer::brush_` struct, not in the ObjC impl.

### Solution: Pass Settings or Store Them

Option 1: Store brush settings in impl when stroke starts
Option 2: Pass settings to a new flush method
Option 3: Add callback from ObjC to C++ when flush needed

## Recommended Implementation

Store current brush settings in the ObjC impl when stroke begins, then use them for auto-flush:

```objc
@interface MetalStampRendererImpl ()
// ... existing properties ...
@property (nonatomic) float currentHardness;
@property (nonatomic) float currentOpacity;
@property (nonatomic) float currentFlow;
@property (nonatomic) float currentGrainScale;
@property (nonatomic) BOOL currentUseShape;
@property (nonatomic) BOOL currentUseGrain;
@end

// In beginStrokeAt: - store settings
// In interpolateFrom: - use stored settings for auto-flush
```

## Files To Modify

1. **metal_renderer.mm** - Add auto-flush in `interpolateFrom:to:` method
2. Store brush settings in impl at stroke start
3. Remove my broken fix in `add_stroke_point()`

---

## Implementation Complete

### Changes Made:

1. **Added stroke properties** for brush settings (lines 91-97):
   - `strokeHardness`, `strokeOpacity`, `strokeFlow`, `strokeGrainScale`
   - `strokeUseShape`, `strokeUseGrain`

2. **Store settings in begin_stroke()** (lines 1456-1464):
   - Save current brush settings when stroke starts

3. **Removed broken fix** in `add_stroke_point()`:
   - Was checking `impl_.pointCount` (raw points) instead of `_points.size()` (interpolated)

4. **Added proper auto-flush in interpolateFrom:to:** (lines 855-864):
   ```objc
   if (_points.size() >= metal_stamp::MAX_POINTS_PER_STROKE - 500) {
       NSLog(@"[Stroke] Auto-flush: %zu points at buffer limit, committing to canvas", _points.size());
       [self renderPointsWithHardness:self.strokeHardness opacity:self.strokeOpacity
                                 flow:self.strokeFlow grainScale:self.strokeGrainScale
                       useShapeTexture:self.strokeUseShape useGrainTexture:self.strokeUseGrain];
       [self commitStrokeToCanvas];  // This clears _points
   }
   ```

### Key Insight

The buffer limit is on **interpolated points** (`_points.size()`), not **raw input points** (`pointCount`).
With small spacing (0.1), one raw point generates ~10 interpolated points.
My first fix checked raw points - completely wrong scale!
