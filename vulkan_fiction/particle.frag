#version 450

// Hand-painted square blob — character color, subtle organic movement

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec2 fragScreenPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    float time;
    float padding;
} pc;

float hash(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = fragUV;
    float t = pc.time;

    // Unique seed per instance
    float seed = hash2(floor(fragScreenPos / 4.0));

    // Center of the square with very gentle drift
    vec2 center = vec2(0.5, 0.5);
    center.x += sin(t * 0.5 + seed * 6.28) * 0.03;
    center.y += cos(t * 0.4 + seed * 4.17) * 0.03;

    // Slightly irregular square shape using smooth box SDF
    vec2 d = abs(uv - center);

    // Wobble the edges slightly for hand-painted feel
    float wobX = sin(uv.y * 12.0 + seed * 30.0) * 0.03;
    float wobY = sin(uv.x * 11.0 + seed * 20.0) * 0.03;
    d.x += wobX;
    d.y += wobY;

    // Rounded square — radius varies per instance
    float r = 0.06 + hash(seed * 71.0) * 0.04;
    float halfSize = 0.32 + hash(seed * 37.0) * 0.06;
    vec2 q = d - halfSize + r;
    float sdf = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;

    // Soft painted edge
    float shape = 1.0 - smoothstep(-0.02, 0.04, sdf);

    // Paint texture — slight opacity variation
    float paint = 0.85 + 0.15 * sin(uv.x * 25.0 + seed * 50.0)
                              * sin(uv.y * 23.0 + seed * 40.0);

    // Character color
    vec3 color = fragColor.rgb;

    // Subtle darkening at edges
    float edgeDark = smoothstep(-0.04, 0.02, sdf);
    color = mix(color, color * 0.7, edgeDark * 0.25);

    float alpha = shape * paint * 0.88;

    // Very subtle pulse
    alpha *= 0.92 + 0.08 * sin(t * 1.1 + seed * 6.28);

    outColor = vec4(color * alpha, alpha);

    if (alpha < 0.005) {
        discard;
    }
}
