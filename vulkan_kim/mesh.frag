#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Camera/Transform UBO
layout(binding = 0) uniform MeshUBO {
    vec4 cameraPos;      // xyz = position, w = fov
    vec4 cameraTarget;   // xyz = target
    vec4 lightDir;       // xyz = direction
    vec4 resolution;     // xy = resolution, z = time
} ubo;

void main() {
    // Normalize the normal (may be unnormalized from interpolation)
    vec3 N = normalize(fragNormal);

    // Light direction (from UBO)
    vec3 L = normalize(ubo.lightDir.xyz);

    // Simple diffuse lighting
    float diff = max(dot(N, L), 0.0);

    // Ambient
    float ambient = 0.3;

    // Wireframe-like color (cyan/teal)
    vec3 baseColor = vec3(0.0, 0.8, 0.9);

    // Final color with lighting
    vec3 color = baseColor * (ambient + diff * 0.7);

    // Semi-transparent for overlay effect
    outColor = vec4(color, 0.7);
}
