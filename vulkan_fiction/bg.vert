#version 450

// Background vertex shader - renders a fullscreen quad with texture
// Uses the same vertex format as text (position, texcoord, color)

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
} pc;

void main() {
    // Convert pixel coordinates to NDC
    vec2 ndc;
    ndc.x = (inPosition.x / pc.screenSize.x) * 2.0 - 1.0;
    ndc.y = (inPosition.y / pc.screenSize.y) * 2.0 - 1.0;
    
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
