struct Uniforms {
    screenSize: vec2<f32>,
    time: f32,
    yFlip: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) color: vec4<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) screenPos: vec2<f32>,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let ndcX = (in.position.x / uniforms.screenSize.x) * 2.0 - 1.0;
    let ndcY = ((in.position.y / uniforms.screenSize.y) * 2.0 - 1.0) * uniforms.yFlip;
    out.position = vec4<f32>(ndcX, ndcY, 0.0, 1.0);
    out.uv = in.texCoord;
    out.color = in.color;
    out.screenPos = in.position;
    return out;
}

fn hash1(p: f32) -> f32 {
    return fract(sin(p * 127.1) * 43758.5453);
}

fn hash2(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let t = uniforms.time;

    let seed = hash2(floor(in.screenPos / 4.0));

    var center = vec2<f32>(0.5, 0.5);
    center.x += sin(t * 0.5 + seed * 6.28) * 0.03 + sin(t * 0.3) * 0.15;
    center.y += cos(t * 0.4 + seed * 4.17) * 0.03;

    var d = abs(uv - center);

    let wobX = sin(uv.y * 12.0 + seed * 30.0) * 0.03;
    let wobY = sin(uv.x * 11.0 + seed * 20.0) * 0.03;
    d.x += wobX;
    d.y += wobY;

    let r = 0.06 + hash1(seed * 71.0) * 0.1;
    let halfSize = 0.32 + hash1(seed * 37.0) * 0.001;
    let q = d - vec2<f32>(halfSize, halfSize) + vec2<f32>(r, r);
    let sdf = length(max(q, vec2<f32>(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - r;

    let shape = 1.0 - smoothstep(-0.02, 0.2, sdf);
    let paint = 0.85 + 0.15 * sin(uv.x * 25.0 + seed * 50.0) * sin(uv.y * 23.0 + seed * 40.0);

    var color = in.color.rgb;
    let edgeDark = smoothstep(-0.04, 0.02, sdf);
    color = mix(color, color * 0.7, edgeDark * 0.25);

    var alpha = shape * paint * 0.93;
    alpha *= 0.97 + 0.15 * sin(t * 4.1 + seed * 7.28);

    if (alpha < 0.005) {
        discard;
    }

    return vec4<f32>(color * alpha, alpha);
}
