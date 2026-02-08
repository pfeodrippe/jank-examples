// Background shader for fiction game (WebGPU/WGSL)
// Renders a fullscreen quad with texture

// Uniforms - replaces Vulkan push constants
struct Uniforms {
    screenSize: vec2<f32>,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var bgTexture: texture_2d<f32>;
@group(0) @binding(2) var bgSampler: sampler;

// Vertex input (matches TextVertex struct - reused for background)
struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) color: vec4<f32>,
}

// Vertex output / Fragment input
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) color: vec4<f32>,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    
    // Convert pixel coordinates to NDC
    var ndc: vec2<f32>;
    ndc.x = (in.position.x / uniforms.screenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (in.position.y / uniforms.screenSize.y) * 2.0;  // Flip Y for WebGPU
    
    out.position = vec4<f32>(ndc, 0.0, 1.0);
    out.texCoord = in.texCoord;
    out.color = in.color;
    
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let texColor = textureSample(bgTexture, bgSampler, in.texCoord);
    // Multiply by vertex color (allows tinting/alpha)
    return texColor * in.color;
}
