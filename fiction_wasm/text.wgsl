// Text shader for fiction dialogue panel (WebGPU/WGSL)
// Renders textured quads with per-vertex color for colored text

// Uniforms - replaces Vulkan push constants
struct Uniforms {
    screenSize: vec2<f32>,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontAtlas: texture_2d<f32>;
@group(0) @binding(2) var fontSampler: sampler;

// Vertex input (matches TextVertex struct)
struct VertexInput {
    @location(0) position: vec2<f32>,   // Screen position in pixels
    @location(1) texCoord: vec2<f32>,   // UV coordinates in font atlas
    @location(2) color: vec4<f32>,      // RGBA color
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
    
    // Convert pixel coordinates to NDC (-1 to 1)
    // WebGPU Y is top = 1, bottom = -1 (opposite of Vulkan)
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
    // Sample font atlas (single channel R8)
    let coverage = textureSample(fontAtlas, fontSampler, in.texCoord).r;
    
    // Apply vertex color with font coverage as alpha
    // For solid rectangles (like backgrounds), the UV is in a solid white region
    let outColor = vec4<f32>(in.color.rgb, in.color.a * coverage);
    
    // Discard fully transparent pixels to avoid depth/blending issues
    if (outColor.a < 0.01) {
        discard;
    }
    
    return outColor;
}
