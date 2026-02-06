#version 450

// Hand-painted underline vertex shader
// Passes screen position to fragment shader for per-instance variation

layout(location = 0) in vec2 inPosition;  // Screen position in pixels
layout(location = 1) in vec2 inTexCoord;  // UV coordinates (0-1 over the quad)
layout(location = 2) in vec4 inColor;     // RGBA color (speaker color)

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec2 fragScreenPos;  // Pixel position for unique seed

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    float time;
    float padding;
} pc;

void main() {
    vec2 ndc;
    ndc.x = (inPosition.x / pc.screenSize.x) * 2.0 - 1.0;
    ndc.y = (inPosition.y / pc.screenSize.y) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragUV = inTexCoord;
    fragColor = inColor;
    fragScreenPos = inPosition;
}
