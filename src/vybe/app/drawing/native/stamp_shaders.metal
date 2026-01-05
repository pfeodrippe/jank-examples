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
// Fragment Shader - Smooth Circular Stamp
// =============================================================================

// Creates a smooth circular brush stamp with soft edges.
// Uses smoothstep for antialiased edges.
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

    // Apply overall opacity
    float4 out_color = in.color;
    out_color.a *= alpha * uniforms.opacity;

    return half4(out_color);
}

// =============================================================================
// Alternative Fragment Shader - Textured Stamp
// =============================================================================

// For custom brush textures (e.g., pencil, watercolor effects)
fragment half4 stamp_textured_fragment(
    PointVertexOutput in [[stage_in]],
    float2 pointCoord [[point_coord]],
    texture2d<float> brushTexture [[texture(0)]],
    sampler brushSampler [[sampler(0)]],
    constant StrokeUniforms& uniforms [[buffer(1)]]
) {
    // Sample brush texture
    float4 texColor = brushTexture.sample(brushSampler, pointCoord);

    // Combine texture with stroke color
    float4 out_color = in.color;
    out_color.a *= texColor.a * uniforms.opacity;

    // Use texture luminance for variation (optional)
    // out_color.rgb *= texColor.rgb;

    if (out_color.a <= 0.001) {
        discard_fragment();
    }

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
