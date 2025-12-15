#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

// Camera/Transform UBO
layout(binding = 0) uniform MeshUBO {
    vec4 cameraPos;      // xyz = position, w = fov
    vec4 cameraTarget;   // xyz = target
    vec4 lightDir;       // xyz = direction
    vec4 resolution;     // xy = resolution, z = time, w = scale
    vec4 options;        // x = useVertexColors (0 or 1)
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

    // Use vertex colors if enabled, otherwise default cyan
    bool useVertexColors = ubo.options.x > 0.5;
    vec3 baseColor;
    if (useVertexColors && (fragColor.r > 0.001 || fragColor.g > 0.001 || fragColor.b > 0.001)) {
        // Vertex colors already include lighting from color sampler
        // Just pass them through without additional lighting
        outColor = vec4(fragColor, 1.0);
        return;
    } else {
        baseColor = vec3(0.0, 0.8, 0.9);
    }

    // Final color with lighting
    vec3 color = baseColor * (ambient + diff * 0.7);

    // Semi-transparent for overlay effect
    outColor = vec4(color, 0.7);
}
