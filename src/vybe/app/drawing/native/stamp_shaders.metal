// Metal Stamp Brush Shaders
// Implements Procreate-style GPU stamp rendering for smooth brush strokes
//
// The stamp approach renders circular "stamps" along the stroke path with
// proper alpha blending for smooth, continuous appearance.

#include <metal_stdlib>
using namespace metal;

// =============================================================================
// Data Structures
// =============================================================================

// Input point data from CPU
struct Point {
    float2 position;  // Already in Metal NDC (-1 to 1)
    float size;       // Point size in pixels
    float4 color;     // RGBA color
};

// Vertex shader output / fragment shader input
struct PointVertexOutput {
    float4 position [[position]];
    float pointSize [[point_size]];
    float4 color;
};

// Uniform data for the entire stroke
struct StrokeUniforms {
    float2 viewportSize;   // Viewport dimensions
    float hardness;        // 0.0 = soft edge, 1.0 = hard edge
    float opacity;         // Overall stroke opacity
    float flow;            // Paint flow rate
    float grainScale;      // Grain texture scale
    float2 grainOffset;    // For moving grain mode
    int useShapeTexture;   // 0 = procedural, 1 = use texture
    int useGrainTexture;   // 0 = no grain, 1 = use grain
};

// =============================================================================
// Vertex Shader - Point Rendering
// =============================================================================

// This shader takes point positions and outputs them for point sprite rendering.
// Each point becomes a circular stamp rendered in the fragment shader.
vertex PointVertexOutput stamp_vertex(
    constant Point* points [[buffer(0)]],
    constant StrokeUniforms& uniforms [[buffer(1)]],
    uint vid [[vertex_id]]
) {
    PointVertexOutput out;

    // Position is already in normalized device coordinates
    out.position = float4(points[vid].position, 0.0, 1.0);

    // Point size determines the diameter of the stamp
    out.pointSize = points[vid].size;

    // Pass through color
    out.color = points[vid].color;

    return out;
}

// =============================================================================
// Fragment Shader - Smooth Circular Stamp (Procedural)
// =============================================================================

// Creates a smooth circular brush stamp with soft edges.
// Uses smoothstep for antialiased edges.
// This is the default shader - procedural circle, no textures needed.
fragment half4 stamp_fragment(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    // pointCoord is (0,0) at top-left, (1,1) at bottom-right of point sprite
    // Convert to centered coordinates where (0,0) is center
    float2 centered = pointCoord - float2(0.5);
    float dist = length(centered) * 2.0;  // Distance from center, 0-1 range for edge

    // Hardness controls the softness of the brush edge
    // hardness = 0: soft gradient from center to edge
    // hardness = 1: hard circular edge
    float inner_radius = uniforms.hardness * 0.9;  // Inner fully-opaque region
    float outer_radius = 1.0;  // Outer edge

    // Smooth falloff from inner to outer radius
    float alpha = 1.0 - smoothstep(inner_radius, outer_radius, dist);

    // Discard pixels outside the circle
    if (alpha <= 0.0) {
        discard_fragment();
    }

    // Apply overall opacity and flow
    float4 out_color = in.color;
    out_color.a *= alpha * uniforms.opacity * uniforms.flow;

    return half4(out_color);
}

// =============================================================================
// Fragment Shader - Huntsman Crayon Effect
// =============================================================================

// Professional crayon brush inspired by Procreate best practices
// Features: rough organic edges, paper grain texture, wax buildup simulation
fragment half4 stamp_fragment_crayon(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    float2 centered = pointCoord - float2(0.5);
    float dist = length(centered) * 2.0;

    // === PAPER GRAIN TEXTURE (Texturized mode - static behind stroke) ===
    // Multi-octave noise for realistic paper grain
    float2 grainCoord = pointCoord * 30.0;  // Scale for paper texture density
    float paper1 = sin(grainCoord.x * 23.7 + grainCoord.y * 17.3) * 0.5 + 0.5;
    float paper2 = sin(grainCoord.x * 41.1 + grainCoord.y * 31.7) * 0.5 + 0.5;
    float paper3 = sin((grainCoord.x * 0.7 + grainCoord.y * 1.3) * 19.1) * 0.5 + 0.5;
    float paper4 = sin((grainCoord.x * 1.1 - grainCoord.y * 0.9) * 29.3) * 0.5 + 0.5;

    // Combine for natural paper texture (different frequencies)
    float paperGrain = paper1 * 0.35 + paper2 * 0.30 + paper3 * 0.20 + paper4 * 0.15;

    // === ROUGH ORGANIC SHAPE (not perfectly circular) ===
    // Add noise to the edge to create organic crayon shape
    float2 edgeNoiseCoord = centered * 8.0;
    float edgeNoise1 = sin(edgeNoiseCoord.x * 7.3 + edgeNoiseCoord.y * 11.7) * 0.08;
    float edgeNoise2 = sin(edgeNoiseCoord.x * 13.1 + edgeNoiseCoord.y * 5.3) * 0.05;
    float edgeDistortion = edgeNoise1 + edgeNoise2;

    // Distorted distance for organic shape
    float organicDist = dist + edgeDistortion;

    // Crayon edge - not too hard, not too soft
    float inner_radius = 0.2 + uniforms.hardness * 0.4;
    float outer_radius = 0.85 + edgeDistortion * 0.5;  // Wobbly outer edge
    float shapeMask = 1.0 - smoothstep(inner_radius, outer_radius, organicDist);

    if (shapeMask <= 0.0) {
        discard_fragment();
    }

    // === WAX BUILDUP SIMULATION ===
    // Crayon deposits more wax in center, less at edges (paper shows through)
    float waxDensity = 1.0 - smoothstep(0.0, 0.7, dist);
    waxDensity = mix(0.6, 1.0, waxDensity);  // Minimum 60% coverage

    // === PAPER TOOTH INTERACTION ===
    // Paper grain affects coverage - high points get more crayon
    float grainStrength = uniforms.grainScale * 0.6;
    float coverage = mix(1.0, paperGrain, grainStrength);

    // Edge breakup - crayon breaks up more at edges where paper shows through
    float edgeBreakup = smoothstep(0.4, 0.9, dist);
    float breakupNoise = paper1 * 0.4 + paper2 * 0.3;
    coverage *= mix(1.0, 0.5 + breakupNoise, edgeBreakup * grainStrength);

    // === SUBTLE COLOR VARIATION (like real crayon) ===
    // Slight darkness variation for more organic look
    float colorVar = 1.0 - (paper3 * 0.08 * grainStrength);

    // === FINAL COMPOSITION ===
    float4 out_color = in.color;
    out_color.rgb *= colorVar;  // Subtle color variation
    out_color.a *= shapeMask * coverage * waxDensity * uniforms.opacity * uniforms.flow;

    // Threshold to avoid too-faint pixels
    if (out_color.a <= 0.008) {
        discard_fragment();
    }

    return half4(out_color);
}

// =============================================================================
// Fragment Shader - Watercolor Effect
// =============================================================================

// Creates a watercolor-like effect with wet edges
fragment half4 stamp_fragment_watercolor(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    float2 centered = pointCoord - float2(0.5);
    float dist = length(centered) * 2.0;

    // Very soft center
    float alpha = 1.0 - smoothstep(0.0, 0.9, dist);

    // Wet edge effect - darker ring near edge
    float edgeRing = smoothstep(0.5, 0.8, dist) * (1.0 - smoothstep(0.8, 1.0, dist));
    alpha = max(alpha, edgeRing * 0.4);

    if (alpha <= 0.0) {
        discard_fragment();
    }

    // Watercolor variation
    float2 noiseCoord = pointCoord * 15.0;
    float variation = sin(noiseCoord.x * 7.3 + noiseCoord.y * 11.7) * 0.15 + 0.85;

    float4 out_color = in.color;
    out_color.a *= alpha * variation * uniforms.opacity * uniforms.flow * 0.5;  // Watercolor is more transparent

    return half4(out_color);
}

// =============================================================================
// Fragment Shader - Marker Effect
// =============================================================================

// Creates a marker-like effect with hard edges and slight streaking
fragment half4 stamp_fragment_marker(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    float2 centered = pointCoord - float2(0.5);
    float dist = length(centered) * 2.0;

    // Very hard edge like a marker
    float alpha = 1.0 - smoothstep(0.85, 0.95, dist);

    if (alpha <= 0.0) {
        discard_fragment();
    }

    // Slight streaking in one direction
    float streak = sin(pointCoord.y * 30.0) * 0.05 + 0.95;

    float4 out_color = in.color;
    out_color.a *= alpha * streak * uniforms.opacity * uniforms.flow;

    return half4(out_color);
}

// =============================================================================
// Alternative Fragment Shader - Textured Stamp
// =============================================================================

// For custom brush textures (e.g., Procreate brush shapes)
// Shape textures are loaded as R8 (grayscale).
// NOTE: CGBitmapContext grayscale loading INVERTS the values, so we invert back here.
// Procreate convention: WHITE=opaque (paint), BLACK=transparent (no paint)
fragment half4 stamp_textured_fragment(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    texture2d<float> brushTexture [[texture(0)]],
    sampler brushSampler [[sampler(0)]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    // Sample brush texture (R8 format: only R channel has data)
    float4 texColor = brushTexture.sample(brushSampler, pointCoord);

    // INVERT the texture value - CGBitmapContext grayscale loading inverts values
    // After inversion: HIGH where source WHITE (paint), LOW where source BLACK (transparent)
    float shapeAlpha = 1.0 - texColor.r;

    // Discard fully transparent pixels
    if (shapeAlpha < 0.01) {
        discard_fragment();
    }

    // Output stroke color with texture-based alpha
    float4 out_color = in.color;
    out_color.a *= shapeAlpha * uniforms.opacity * uniforms.flow;

    return half4(out_color);
}

// =============================================================================
// Line Segment Expansion Vertex Shader (for stroke quads)
// =============================================================================

// Input for line-based rendering
struct LineVertex {
    float2 position;       // Current point position (NDC)
    float2 prevPosition;   // Previous point position (for direction)
    float radius;          // Half-width of stroke at this point
    float4 color;
    float distance;        // Cumulative distance along stroke
};

struct LineVertexOutput {
    float4 position [[position]];
    float4 color;
    float2 localPos;       // Position within quad (-1 to 1)
    float distance;
    float radius;
};

// Expands line segments into quads for stamp-based rendering
vertex LineVertexOutput stroke_quad_vertex(
    constant LineVertex* vertices [[buffer(0)]],
    constant StrokeUniforms& uniforms [[buffer(1)]],
    uint vid [[vertex_id]],
    uint iid [[instance_id]]
) {
    LineVertexOutput out;

    LineVertex v = vertices[iid];

    // Calculate perpendicular direction for quad expansion
    float2 dir = normalize(v.position - v.prevPosition);
    float2 perp = float2(-dir.y, dir.x);

    // Expand to quad corners (triangle strip order)
    float2 offset;
    switch (vid % 4) {
        case 0: offset = -perp * v.radius - dir * v.radius; break;
        case 1: offset = +perp * v.radius - dir * v.radius; break;
        case 2: offset = -perp * v.radius + dir * v.radius; break;
        case 3: offset = +perp * v.radius + dir * v.radius; break;
    }

    // Convert offset from pixel space to NDC (approximate)
    float2 ndcOffset = offset / uniforms.viewportSize * 2.0;

    out.position = float4(v.position + ndcOffset, 0.0, 1.0);
    out.color = v.color;
    out.localPos = offset / v.radius;  // -1 to 1 within quad
    out.distance = v.distance;
    out.radius = v.radius;

    return out;
}

// Fragment shader for quad-based stroke rendering with stamp accumulation
fragment half4 stroke_quad_fragment(
    LineVertexOutput in [[stage_in]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    // Distance from the stroke centerline
    float dist_from_center = length(in.localPos);

    // Soft edge falloff
    float inner = uniforms.hardness * 0.8;
    float alpha = 1.0 - smoothstep(inner, 1.0, dist_from_center);

    if (alpha <= 0.0) {
        discard_fragment();
    }

    float4 out_color = in.color;
    out_color.a *= alpha * uniforms.opacity;

    return half4(out_color);
}

// =============================================================================
// Clear shader (for clearing canvas to background color)
// =============================================================================

struct ClearVertex {
    float4 position [[position]];
    float4 color;
};

vertex ClearVertex clear_vertex(
    uint vid [[vertex_id]],
    constant float4& clearColor [[buffer(0)]]
) {
    ClearVertex out;

    // Full-screen quad
    float2 positions[4] = {
        float2(-1, -1),
        float2( 1, -1),
        float2(-1,  1),
        float2( 1,  1)
    };

    out.position = float4(positions[vid], 0.0, 1.0);
    out.color = clearColor;

    return out;
}

fragment half4 clear_fragment(ClearVertex in [[stage_in]]) {
    return half4(in.color);
}

// =============================================================================
// Canvas Blit Shader (with pan/zoom/rotate transform)
// =============================================================================

struct CanvasTransformUniforms {
    float2 pan;           // Pan offset in pixels
    float scale;          // Zoom level
    float rotation;       // Rotation in radians
    float2 pivot;         // Transform pivot in pixels
    float2 viewportSize;  // Viewport size in pixels
};

struct CanvasBlitVertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex CanvasBlitVertexOut canvas_blit_vertex(
    uint vid [[vertex_id]],
    constant CanvasTransformUniforms& transform [[buffer(0)]]
) {
    // Full-screen quad corners (NDC)
    float2 corners[4] = {
        float2(-1, -1),  // BL
        float2( 1, -1),  // BR
        float2(-1,  1),  // TL
        float2( 1,  1)   // TR
    };

    float2 pos = corners[vid];

    // Convert NDC to screen pixel coordinates
    // NDC (-1,-1) = bottom-left, (1,1) = top-right
    // Screen (0,0) = top-left, (width,height) = bottom-right
    float2 screenPos;
    screenPos.x = (pos.x + 1.0) * 0.5 * transform.viewportSize.x;
    screenPos.y = (1.0 - pos.y) * 0.5 * transform.viewportSize.y;

    // Apply inverse transform to get canvas position
    // 1. Translate to pivot
    float2 p = screenPos - transform.pivot;

    // 2. Undo pan
    p = p - transform.pan;

    // 3. Undo scale
    p = p / transform.scale;

    // 4. Undo rotation
    float c = cos(-transform.rotation);
    float s = sin(-transform.rotation);
    p = float2(p.x * c - p.y * s, p.x * s + p.y * c);

    // 5. Translate back from pivot
    p = p + transform.pivot;

    // Convert to UV (0 to 1)
    float2 uv = p / transform.viewportSize;

    CanvasBlitVertexOut out;
    out.position = float4(corners[vid], 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment half4 canvas_blit_fragment(
    CanvasBlitVertexOut in [[stage_in]],
    texture2d<float> canvasTexture [[texture(0)]],
    sampler canvasSampler [[sampler(0)]]
) {
    // Check if UV is outside canvas bounds (show transparent/background)
    if (in.uv.x < 0.0 || in.uv.x > 1.0 || in.uv.y < 0.0 || in.uv.y > 1.0) {
        // Return a subtle gray for out-of-bounds (canvas edge)
        return half4(0.3, 0.3, 0.3, 1.0);
    }

    float4 color = canvasTexture.sample(canvasSampler, in.uv);
    return half4(color);
}

// =============================================================================
// UI Rectangle Shader (for sliders and buttons)
// =============================================================================

struct UIRectParams {
    float4 rect;       // x, y, width, height in NDC
    float4 color;
    float cornerRadius;  // In NDC units
};

struct UIVertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex UIVertexOut ui_rect_vertex(uint vid [[vertex_id]], constant UIRectParams& params [[buffer(0)]]) {
    // Quad corners: 0=BL, 1=BR, 2=TL, 3=TR
    float2 corners[4] = { float2(0,0), float2(1,0), float2(0,1), float2(1,1) };
    float2 uv = corners[vid];

    float2 pos = params.rect.xy + uv * params.rect.zw;

    UIVertexOut out;
    out.position = float4(pos, 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment half4 ui_rect_fragment(UIVertexOut in [[stage_in]], constant UIRectParams& params [[buffer(0)]]) {
    // Simple rounded rectangle SDF
    float2 size = params.rect.zw;
    float2 center = float2(0.5, 0.5);
    float2 p = in.uv - center;

    // Aspect-correct radius
    float r = params.cornerRadius / min(size.x, size.y);
    float2 q = abs(p) - (float2(0.5) - r);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;

    // Antialiased edge
    float aa = fwidth(d) * 1.5;
    float alpha = 1.0 - smoothstep(-aa, aa, d);

    return half4(half3(params.color.rgb), half(params.color.a * alpha));
}

// ============================================
// Textured Rectangle Shaders (for brush thumbnails)
// ============================================

struct UITexturedRectParams {
    float4 rect;       // x, y, width, height in NDC
    float4 tint;       // RGBA tint color (multiplied with texture)
    int textureId;     // Reference to texture (not used in shader directly)
};

vertex UIVertexOut ui_textured_rect_vertex(uint vid [[vertex_id]], constant UITexturedRectParams& params [[buffer(0)]]) {
    float2 corners[4] = { float2(0,0), float2(1,0), float2(0,1), float2(1,1) };
    float2 uv = corners[vid];

    float2 pos = params.rect.xy + uv * params.rect.zw;

    UIVertexOut out;
    out.position = float4(pos, 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment half4 ui_textured_rect_fragment(UIVertexOut in [[stage_in]],
                                         constant UITexturedRectParams& params [[buffer(0)]],
                                         texture2d<float> tex [[texture(0)]],
                                         sampler s [[sampler(0)]]) {
    // Flip Y for correct texture orientation
    float2 texCoord = float2(in.uv.x, 1.0 - in.uv.y);
    float4 texColor = tex.sample(s, texCoord);
    float4 result = texColor * params.tint;
    return half4(result);
}
