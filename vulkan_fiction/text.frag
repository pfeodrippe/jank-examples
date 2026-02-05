#version 450

// Text fragment shader for fiction dialogue panel
// Samples from font atlas (single-channel) and applies vertex color

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

// Font atlas texture (R8 format - single channel for glyph coverage)
layout(binding = 0) uniform sampler2D fontAtlas;

void main() {
    // Sample font atlas (single channel R8)
    float coverage = texture(fontAtlas, fragTexCoord).r;
    
    // Apply vertex color with font coverage as alpha
    // For solid rectangles (like backgrounds), the UV is in a solid white region
    outColor = vec4(fragColor.rgb, fragColor.a * coverage);
    
    // Discard fully transparent pixels to avoid depth/blending issues
    if (outColor.a < 0.01) {
        discard;
    }
}
