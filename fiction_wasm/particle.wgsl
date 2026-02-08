// Hand-painted square blob shader for fiction game (WebGPU/WGSL)
// Character color with subtle organic movement

// Uniforms - replaces Vulkan push constants
struct Uniforms {
    screenSize: vec2<f32>,
    time: f32,
    padding: f32,  // Padding for alignment
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// Vertex input
struct VertexInput {
    @location(0) position: vec2<f32>,   // Screen position in pixels
    @location(1) texCoord: vec2<f32>,   // UV coordinates (0-1 over the quad)
    @location(2) color: vec4<f32>,      // RGBA color (speaker color)
}

// Vertex output / Fragment input
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) screenPos: vec2<f32>,  // Pixel position for unique seed
}

fn hash(p: f32) -> f32 {
    return fract(sin(p * 127.1) * 43758.5453);
}

fn hash2(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453);
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    
    var ndc: vec2<f32>;
    ndc.x = (in.position.x / uniforms.screenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (in.position.y / uniforms.screenSize.y) * 2.0;  // Flip Y for WebGPU
    
    out.position = vec4<f32>(ndc, 0.0, 1.0);
    out.uv = in.texCoord;
    out.color = in.color;
    out.screenPos = in.position;
    
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let t = uniforms.time;
    
    // Unique seed per instance
    let seed = hash2(floor(in.screenPos / 4.0));
    
    // Center of the square with gentle drift - oscillating left/right
    var center = vec2<f32>(0.5, 0.5);
    center.x = center.x + sin(t * 0.5 + seed * 6.28) * 0.03 + sin(t * 0.3) * 0.15;  // Oscillate left-right
    center.y = center.y + cos(t * 0.4 + seed * 4.17) * 0.03;
    
    // Slightly irregular square shape using smooth box SDF
    var d = abs(uv - center);
    
    // Wobble the edges slightly for hand-painted feel
    let wobX = sin(uv.y * 12.0 + seed * 30.0) * 0.03;
    let wobY = sin(uv.x * 11.0 + seed * 20.0) * 0.03;
    d.x = d.x + wobX;
    d.y = d.y + wobY;
    
    // Rounded square - radius varies per instance
    let r = 0.06 + hash(seed * 71.0) * 0.1;
    let halfSize = 0.32 + hash(seed * 37.0) * 0.001;
    let q = d - halfSize + r;
    let sdf = length(max(q, vec2<f32>(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - r;
    
    // Soft painted edge
    let shape = 1.0 - smoothstep(-0.02, 0.2, sdf);
    
    // Paint texture - slight opacity variation
    let paint = 0.85 + 0.15 * sin(uv.x * 25.0 + seed * 50.0)
                              * sin(uv.y * 23.0 + seed * 40.0);
    
    // Character color
    var color = in.color.rgb;
    
    // Subtle darkening at edges
    let edgeDark = smoothstep(-0.04, 0.02, sdf);
    color = mix(color, color * 0.7, edgeDark * 0.25);
    
    var alpha = shape * paint * 0.93;
    
    // Very subtle pulse
    alpha = alpha * (0.97 + 0.15 * sin(t * 4.1 + seed * 7.28));
    
    // Discard fully transparent pixels
    if (alpha < 0.005) {
        discard;
    }
    
    return vec4<f32>(color * alpha, alpha);
}
