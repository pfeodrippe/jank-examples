#version 450

// Background fragment shader - samples RGBA texture directly

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

// Background image texture (RGBA)
layout(binding = 0) uniform sampler2D bgTexture;

void main() {
    vec4 texColor = texture(bgTexture, fragTexCoord);
    // Multiply by vertex color (allows tinting/alpha)
    outColor = texColor * fragColor;
}
