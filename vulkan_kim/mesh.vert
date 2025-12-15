#version 450

// Vertex input
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

// Output to fragment shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragColor;

// Camera/Transform UBO - matches main UBO structure
layout(binding = 0) uniform MeshUBO {
    vec4 cameraPos;      // xyz = position, w = fov
    vec4 cameraTarget;   // xyz = target
    vec4 lightDir;       // xyz = direction
    vec4 resolution;     // xy = resolution, z = time, w = scale
    vec4 options;        // x = useVertexColors (0 or 1)
} ubo;

// Simple view-projection calculation
mat4 lookAt(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);
    vec3 r = normalize(cross(f, up));
    vec3 u = cross(r, f);

    mat4 m = mat4(
        vec4(r.x, u.x, -f.x, 0.0),
        vec4(r.y, u.y, -f.y, 0.0),
        vec4(r.z, u.z, -f.z, 0.0),
        vec4(-dot(r, eye), -dot(u, eye), dot(f, eye), 1.0)
    );
    return m;
}

mat4 perspective(float focalLength, float aspect, float near, float far) {
    // SDF uses fov as focal length (Z component of ray direction)
    // Perspective projection: focalLength determines field of view
    mat4 m = mat4(0.0);
    m[0][0] = focalLength / aspect;
    m[1][1] = focalLength;  // No Y flip - SDF uses negative up vector
    m[2][2] = far / (near - far);
    m[2][3] = -1.0;
    m[3][2] = (far * near) / (near - far);
    return m;
}

void main() {
    vec3 eye = ubo.cameraPos.xyz;
    vec3 target = ubo.cameraTarget.xyz;
    float fov = ubo.cameraPos.w;
    float aspect = ubo.resolution.x / ubo.resolution.y;
    float scale = ubo.resolution.w;  // Mesh scale from UI

    // Match SDF raymarcher's coordinate system (Y-down up vector)
    mat4 view = lookAt(eye, target, vec3(0.0, -1.0, 0.0));
    mat4 proj = perspective(fov, aspect, 0.01, 100.0);

    vec3 scaledPos = inPosition * scale;
    vec4 worldPos = vec4(scaledPos, 1.0);
    gl_Position = proj * view * worldPos;

    fragNormal = inNormal;
    fragWorldPos = scaledPos;
    fragColor = inColor;
}
