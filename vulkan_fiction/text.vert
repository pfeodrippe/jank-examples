#version 450

// Text vertex shader for fiction dialogue panel
// Renders textured quads with per-vertex color for colored text

// Vertex input (matches TextVertex struct in fiction_text.hpp)
layout(location = 0) in vec2 inPosition;  // Screen position in pixels
layout(location = 1) in vec2 inTexCoord;  // UV coordinates in font atlas
layout(location = 2) in vec4 inColor;     // RGBA color

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// Screen dimensions for pixel-to-NDC conversion
layout(push_constant) uniform PushConstants {
    vec2 screenSize;  // Width, Height
} pc;

void main() {
    // Convert pixel coordinates to NDC (-1 to 1)
    // Vulkan Y is flipped (top = -1, bottom = 1)
    vec2 ndc;
    ndc.x = (inPosition.x / pc.screenSize.x) * 2.0 - 1.0;
    ndc.y = (inPosition.y / pc.screenSize.y) * 2.0 - 1.0;
    
    gl_Position = vec4(ndc, 0.0, 1.0);
    
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
