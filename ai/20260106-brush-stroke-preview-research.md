# Brush Stroke Preview Research

## Summary

Research on how Procreate brush previews work and techniques for generating brush stroke previews programmatically.

---

## 1. Procreate Brushset File Structure

### File Format Overview

Procreate uses three file formats:
- `.brush` - Single brush file
- `.brushset` - Collection of brushes
- `.brushlibrary` - Library of brush sets

**Key insight**: These files are **renamed ZIP archives**. Renaming the extension to `.zip` allows inspection of internal contents.

### Internal Structure of a `.brush` File

```
brush_name.brush/
├── Brush.archive      # Binary plist (bplist) with brush configuration
├── Grain.png          # Brush texture/grain image (inverted - inked parts white)
├── Shape.png          # Brush tip shape (optional)
├── QuickLook/
│   └── Thumbnail.png  # Preview thumbnail
└── (optional assets like author picture, signature)
```

### Brush.archive (Binary Plist)

The `Brush.archive` file contains all brush parameters:
- Pressure curves and sensitivity settings
- Texture blend modes
- Shape dynamics (scatter, count, roundness)
- Grain/texture settings (moving vs. texturized, depth, blend modes)
- Taper settings (pressure taper, touch taper)
- Preview configuration (stamp vs. stroke, preview size)

**Working with Brush.archive**:
```bash
# Convert binary plist to XML for reading
plutil -convert xml1 Brush.archive -o Brush.xml

# Convert back to binary
plutil -convert binary1 Brush.xml -o Brush.archive
```

### Brush Preview Configuration (from Procreate Handbook)

The brush preview is a **standardized demo stroke** that:
- Moves from **left to right**
- **Increases pressure throughout** the stroke
- **Drops off pressure towards the end**

This ensures pressure-linked effects display properly in the Brush Library.

Preview options:
- **Stamp vs. Stroke toggle**: Display single shape stamp or animated stroke
- **Pressure simulation**: Minimum pressure and pressure scale sliders
- **Preview size**: Adjustable size for library display

---

## 2. Techniques for Generating Brush Stroke Previews Programmatically

### Approach 1: Velocity-Based Width (Hooke's Law / Spring Simulation)

Uses physics simulation to create natural stroke variation.

**Core Algorithm**:
```javascript
// Spring-based position smoothing
vx += (mouseX - x) * spring;  // spring constant ~0.5
vy += (mouseY - y) * spring;

// Apply friction to prevent oscillation
vx *= friction;  // friction between 0-1
vy *= friction;

// Calculate velocity magnitude
v += sqrt(vx*vx + vy*vy) - v;
v *= 0.6;

// Width inversely proportional to velocity
strokeWidth = brushSize - v;
```

**Key insight**: Faster drawing = higher velocity = thinner stroke. This creates natural calligraphy-style variation.

### Approach 2: Variable Width Bezier Strokes

Generate offset curves on both sides of the stroke path.

**Algorithm Steps**:
1. Maintain a point buffer (typically 4-5 points)
2. For each segment, calculate perpendicular offset vectors
3. Scale offset distance based on pressure/velocity
4. Generate cubic Bezier curves connecting offset points
5. Fill the closed path between offset curves

**Perpendicular Offset Calculation**:
```javascript
// Direction vector
dx = p2.x - p1.x;
dy = p2.y - p1.y;

// Perpendicular (rotate 90 degrees)
perpX = -dy * (width / 2);
perpY = dx * (width / 2);

// Offset points on both sides
leftPoint = { x: p1.x + perpX, y: p1.y + perpY };
rightPoint = { x: p1.x - perpX, y: p1.y - perpY };
```

### Approach 3: Path Simplification + Curve Fitting

For smooth, editable vector strokes.

**Step 1: Ramer-Douglas-Peucker Simplification**
```javascript
function simplify(points, threshold) {
    // Find point with maximum distance from line
    let maxDist = 0, maxIndex = 0;
    for (let i = 1; i < points.length - 1; i++) {
        const dist = perpendicularDistance(points[i],
                                           points[0],
                                           points[points.length - 1]);
        if (dist > maxDist) {
            maxDist = dist;
            maxIndex = i;
        }
    }

    if (maxDist > threshold) {
        // Recursively simplify both halves
        const left = simplify(points.slice(0, maxIndex + 1), threshold);
        const right = simplify(points.slice(maxIndex), threshold);
        return left.slice(0, -1).concat(right);
    }
    return [points[0], points[points.length - 1]];
}
```

**Step 2: Bezier Curve Fitting** (Philip J. Schneider algorithm from Graphics Gems)
- Use least-squares minimization
- Chord-length parameterization
- Newton's Method for refinement

### Approach 4: Catmull-Rom Spline Smoothing

Interpolating spline that passes through all control points.

**Centripetal Catmull-Rom** (recommended - prevents loops and cusps):
```javascript
function catmullRom(p0, p1, p2, p3, t, alpha = 0.5) {
    // Calculate knot sequence
    const t0 = 0;
    const t1 = t0 + Math.pow(distance(p0, p1), alpha);
    const t2 = t1 + Math.pow(distance(p1, p2), alpha);
    const t3 = t2 + Math.pow(distance(p2, p3), alpha);

    // Interpolate
    const tt = lerp(t1, t2, t);

    const A1 = lerp2D(p0, p1, (tt - t0) / (t1 - t0));
    const A2 = lerp2D(p1, p2, (tt - t1) / (t2 - t1));
    const A3 = lerp2D(p2, p3, (tt - t2) / (t3 - t2));

    const B1 = lerp2D(A1, A2, (tt - t0) / (t2 - t0));
    const B2 = lerp2D(A2, A3, (tt - t1) / (t3 - t1));

    return lerp2D(B1, B2, (tt - t1) / (t2 - t1));
}
```

---

## 3. Best Practices for Brush Preview Strokes

### Standard Preview Stroke Shape

Based on Procreate's approach, a good preview stroke should:

1. **S-curve or wave shape**: Shows both thick and thin areas
2. **Pressure ramp-up**: Start light, increase to full pressure in middle
3. **Pressure drop-off**: Taper at the end
4. **Consistent direction**: Left-to-right (for Western apps)

### Simulating Pressure for Preview

**Typical pressure curve for preview**:
```javascript
function previewPressure(t) {
    // t ranges from 0 to 1 along the stroke
    if (t < 0.1) {
        // Start taper - ramp up
        return t / 0.1 * 0.3 + 0.1;
    } else if (t < 0.7) {
        // Main body - full pressure
        return 0.4 + (t - 0.1) / 0.6 * 0.6;
    } else {
        // End taper - ramp down
        return 1.0 - (t - 0.7) / 0.3 * 0.8;
    }
}
```

### Preview Path Generation

**S-curve or wave using cubic Bezier**:
```javascript
// Simple S-curve for brush preview
const previewPath = [
    { x: 0, y: 50 },           // Start
    { x: 30, y: 30 },          // Control point 1 (curve up)
    { x: 70, y: 70 },          // Control point 2 (curve down)
    { x: 100, y: 50 }          // End
];

// Sample the curve at regular intervals
for (let t = 0; t <= 1; t += 0.01) {
    const point = cubicBezier(previewPath, t);
    const pressure = previewPressure(t);
    drawBrushDab(point.x, point.y, pressure);
}
```

---

## 4. Libraries and Tools

### Squiggy (JavaScript)

Lightweight vector brushstroke library with three brush types:
- **tube_brush**: Variable radius circle trace (traditional paintbrush)
- **stamp_brush**: Fixed convex shape trace (markers, calligraphy)
- **custom_brush**: Morphing arbitrary shapes

Features:
- Catmull-Rom spline preprocessing
- Resampling for equidistant points
- Gaussian blur smoothing
- Self-intersection removal

GitHub: https://github.com/LingDong-/squiggy

### Procreate Brush Conversion Tools

- **Brushporter**: Procreate to Krita converter plugin
- **Procreate to Krita Brush Converter** (KDE GitLab)
- **geologic-patterns** (davenquinn): Python script for creating Procreate brushes programmatically

---

## 5. Commands Used

```bash
# Search queries performed via WebSearch tool
# WebFetch used to extract detailed content from:
#   - gorillasun.de (Hooke's Law brush simulation)
#   - losingfight.com (vector brush implementation)
#   - github.com/davenquinn/geologic-patterns
#   - krita-artists.org (Procreate brush import plugin)
#   - help.procreate.com (Brush Studio Settings)
#   - github.com/LingDong-/squiggy
```

---

## 6. Next Steps

1. **Implement basic preview stroke renderer** using velocity-based width variation
2. **Create S-curve path generator** for consistent preview display
3. **Add pressure simulation** that ramps up in the middle and tapers at ends
4. **Consider Catmull-Rom smoothing** for input point interpolation
5. **Optionally parse Procreate .brush files** to extract preview settings and images

---

## Sources

- [Procreate Brush Studio Settings Handbook](https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings)
- [Simulating Brush Strokes with Hooke's Law](https://www.gorillasun.de/blog/simulating-brush-strokes-with-hookes-law-in-p5js-and-processing/)
- [How to Implement a Vector Brush](https://losingfight.com/blog/2011/05/31/how-to-implement-a-vector-brush/)
- [Squiggy Vector Brushstroke Library](https://github.com/LingDong-/squiggy)
- [Procreate Brush Creation Script](https://github.com/davenquinn/geologic-patterns/blob/master/procreate-brushes/create-brushes)
- [Krita Procreate Import Plugin Discussion](https://krita-artists.org/t/procreate-brush-import-plugin-prototype/104894)
- [Centripetal Catmull-Rom Spline Wikipedia](https://en.wikipedia.org/wiki/Centripetal_Catmull–Rom_spline)
- [Real Time Fitting of Hand-Sketched Pressure Brushstrokes (Pudet, 1994)](https://onlinelibrary.wiley.com/doi/abs/10.1111/1467-8659.1330205)
