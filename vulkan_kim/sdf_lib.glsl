// ============================================================================
// SDF_LIB.GLSL - Comprehensive Signed Distance Functions Library
// ============================================================================
//
// A complete library of SDF primitives, boolean operations, domain
// transformations, and procedural utilities for shader-based modeling.
//
// Usage: #include "sdf_lib.glsl" at the top of your compute shader
//
// References:
//   - Inigo Quilez: https://iquilezles.org/articles/distfunctions/
//   - hg_sdf: https://mercury.sexy/hg_sdf/
//   - The Book of Shaders: https://thebookofshaders.com/
//
// ============================================================================

#ifndef SDF_LIB_GLSL
#define SDF_LIB_GLSL

// ============================================================================
// SECTION 1: HELPER FUNCTIONS
// ============================================================================

// Dot product of vector with itself (squared length)
float dot2(vec2 v) { return dot(v, v); }
float dot2(vec3 v) { return dot(v, v); }

// Normalized dot for 2D
float ndot(vec2 a, vec2 b) { return a.x * b.x - a.y * b.y; }

// Alternative metrics for interesting shapes
float length2(vec3 p) { p = p * p; return sqrt(p.x + p.y + p.z); }
float length6(vec3 p) { p = p * p * p; p = p * p; return pow(p.x + p.y + p.z, 1.0 / 6.0); }
float length8(vec3 p) { p = p * p; p = p * p; p = p * p; return pow(p.x + p.y + p.z, 1.0 / 8.0); }

// ============================================================================
// SECTION 2: ROTATION MATRICES
// ============================================================================

mat3 sdfRotateX(float a) {
    float c = cos(a), s = sin(a);
    return mat3(1, 0, 0, 0, c, -s, 0, s, c);
}

mat3 sdfRotateY(float a) {
    float c = cos(a), s = sin(a);
    return mat3(c, 0, s, 0, 1, 0, -s, 0, c);
}

mat3 sdfRotateZ(float a) {
    float c = cos(a), s = sin(a);
    return mat3(c, -s, 0, s, c, 0, 0, 0, 1);
}

mat2 sdfRotate2D(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c);
}

// ============================================================================
// SECTION 3: 2D SDF PRIMITIVES
// ============================================================================

// Circle (exact)
float sd2Circle(vec2 p, float r) {
    return length(p) - r;
}

// Box (exact)
float sd2Box(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Rounded Box (exact)
float sd2RoundBox(vec2 p, vec2 b, vec4 r) {
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x = (p.y > 0.0) ? r.x : r.y;
    vec2 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

// Line Segment (exact)
float sd2Segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Equilateral Triangle (exact)
float sd2EquilateralTriangle(vec2 p, float r) {
    const float k = sqrt(3.0);
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

// Isosceles Triangle (exact)
float sd2TriangleIsosceles(vec2 p, vec2 q) {
    p.x = abs(p.x);
    vec2 a = p - q * clamp(dot(p, q) / dot(q, q), 0.0, 1.0);
    vec2 b = p - q * vec2(clamp(p.x / q.x, 0.0, 1.0), 1.0);
    float s = -sign(q.y);
    vec2 d = min(vec2(dot(a, a), s * (p.x * q.y - p.y * q.x)),
                 vec2(dot(b, b), s * (p.y - q.y)));
    return -sqrt(d.x) * sign(d.y);
}

// Regular Pentagon (exact)
float sd2Pentagon(vec2 p, float r) {
    const vec3 k = vec3(0.809016994, 0.587785252, 0.726542528);
    p.x = abs(p.x);
    p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
    p -= 2.0 * min(dot(vec2(k.x, k.y), p), 0.0) * vec2(k.x, k.y);
    p -= vec2(clamp(p.x, -r * k.z, r * k.z), r);
    return length(p) * sign(p.y);
}

// Regular Hexagon (exact)
float sd2Hexagon(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

// Regular Octagon (exact)
float sd2Octagon(vec2 p, float r) {
    const vec3 k = vec3(-0.9238795325, 0.3826834323, 0.4142135623);
    p = abs(p);
    p -= 2.0 * min(dot(vec2(k.x, k.y), p), 0.0) * vec2(k.x, k.y);
    p -= 2.0 * min(dot(vec2(-k.x, k.y), p), 0.0) * vec2(-k.x, k.y);
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}

// Hexagram / Star of David (exact)
float sd2Hexagram(vec2 p, float r) {
    const vec4 k = vec4(-0.5, 0.8660254038, 0.5773502692, 1.7320508076);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= 2.0 * min(dot(k.yx, p), 0.0) * k.yx;
    p -= vec2(clamp(p.x, r * k.z, r * k.w), r);
    return length(p) * sign(p.y);
}

// N-pointed Star (exact)
float sd2Star(vec2 p, float r, int n, float m) {
    float an = 3.141593 / float(n);
    float en = 3.141593 / m;
    vec2 acs = vec2(cos(an), sin(an));
    vec2 ecs = vec2(cos(en), sin(en));
    float bn = mod(atan(p.x, p.y), 2.0 * an) - an;
    p = length(p) * vec2(cos(bn), abs(sin(bn)));
    p -= r * acs;
    p += ecs * clamp(-dot(p, ecs), 0.0, r * acs.y / ecs.y);
    return length(p) * sign(p.x);
}

// Heart (exact)
float sd2Heart(vec2 p) {
    p.x = abs(p.x);
    if (p.y + p.x > 1.0) return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}

// Moon (exact)
float sd2Moon(vec2 p, float d, float ra, float rb) {
    p.y = abs(p.y);
    float a = (ra * ra - rb * rb + d * d) / (2.0 * d);
    float b = sqrt(max(ra * ra - a * a, 0.0));
    if (d * (p.x * b - p.y * a) > d * d * max(b - p.y, 0.0)) return length(p - vec2(a, b));
    return max((length(p) - ra), -(length(p - vec2(d, 0)) - rb));
}

// Arc (exact)
float sd2Arc(vec2 p, vec2 sc, float ra, float rb) {
    p.x = abs(p.x);
    return ((sc.y * p.x > sc.x * p.y) ? length(p - sc * ra) : abs(length(p) - ra)) - rb;
}

// Pie / Sector (exact)
float sd2Pie(vec2 p, vec2 c, float r) {
    p.x = abs(p.x);
    float l = length(p) - r;
    float m = length(p - c * clamp(dot(p, c), 0.0, r));
    return max(l, m * sign(c.y * p.x - c.x * p.y));
}

// Horseshoe (exact)
float sd2Horseshoe(vec2 p, vec2 c, float r, vec2 w) {
    p.x = abs(p.x);
    float l = length(p);
    p = mat2(-c.x, c.y, c.y, c.x) * p;
    p = vec2((p.y > 0.0 || p.x > 0.0) ? p.x : l * sign(-c.x), (p.x > 0.0) ? p.y : l);
    p = vec2(p.x, abs(p.y - r)) - w;
    return length(max(p, 0.0)) + min(0.0, max(p.x, p.y));
}

// Vesica (exact)
float sd2Vesica(vec2 p, float w, float h) {
    float d = 0.5 * (w * w - h * h) / h;
    p = abs(p);
    vec3 c = (w * p.y < d * (p.x - w)) ? vec3(0.0, w, 0.0) : vec3(-d, 0.0, d + h);
    return length(p - c.yx) - c.z;
}

// Ellipse (bound - Newton approximation)
float sd2Ellipse(vec2 p, vec2 ab) {
    p = abs(p);
    if (p.x > p.y) { p = p.yx; ab = ab.yx; }
    float l = ab.y * ab.y - ab.x * ab.x;
    float m = ab.x * p.x / l, m2 = m * m;
    float n = ab.y * p.y / l, n2 = n * n;
    float c = (m2 + n2 - 1.0) / 3.0, c3 = c * c * c;
    float q = c3 + m2 * n2 * 2.0;
    float d = c3 + m2 * n2;
    float g = m + m * n2;
    float co;
    if (d < 0.0) {
        float h = acos(q / c3) / 3.0;
        float s = cos(h);
        float t = sin(h) * sqrt(3.0);
        float rx = sqrt(-c * (s + t + 2.0) + m2);
        float ry = sqrt(-c * (s - t + 2.0) + m2);
        co = (ry + sign(l) * rx + abs(g) / (rx * ry) - m) / 2.0;
    } else {
        float h = 2.0 * m * n * sqrt(d);
        float s = sign(q + h) * pow(abs(q + h), 1.0 / 3.0);
        float u = sign(q - h) * pow(abs(q - h), 1.0 / 3.0);
        float rx = -s - u - c * 4.0 + 2.0 * m2;
        float ry = (s - u) * sqrt(3.0);
        float rm = sqrt(rx * rx + ry * ry);
        co = (ry / sqrt(rm - rx) + 2.0 * g / rm - m) / 2.0;
    }
    vec2 r = ab * vec2(co, sqrt(1.0 - co * co));
    return length(r - p) * sign(p.y - r.y);
}

// Cross (exact)
float sd2Cross(vec2 p, vec2 b, float r) {
    p = abs(p);
    p = (p.y > p.x) ? p.yx : p.xy;
    vec2 q = p - b;
    float k = max(q.y, q.x);
    vec2 w = (k > 0.0) ? q : vec2(b.y - p.x, -k);
    return sign(k) * length(max(w, 0.0)) + r;
}

// Rhombus (exact)
float sd2Rhombus(vec2 p, vec2 b) {
    b.y = -b.y;
    p = abs(p);
    float h = clamp((dot(b, p) + b.y * b.y) / dot(b, b), 0.0, 1.0);
    p -= b * vec2(h, h - 1.0);
    return length(p) * sign(p.x);
}

// Trapezoid (exact)
float sd2Trapezoid(vec2 p, float r1, float r2, float he) {
    vec2 k1 = vec2(r2, he);
    vec2 k2 = vec2(r2 - r1, 2.0 * he);
    p.x = abs(p.x);
    vec2 ca = vec2(p.x - min(p.x, (p.y < 0.0) ? r1 : r2), abs(p.y) - he);
    vec2 cb = p - k1 + k2 * clamp(dot(k1 - p, k2) / dot2(k2), 0.0, 1.0);
    float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
    return s * sqrt(min(dot2(ca), dot2(cb)));
}

// Parallelogram (exact)
float sd2Parallelogram(vec2 p, float wi, float he, float sk) {
    vec2 e = vec2(sk, he);
    p = (p.y < 0.0) ? -p : p;
    vec2 w = p - e;
    w.x -= clamp(w.x, -wi, wi);
    vec2 d = vec2(dot(w, w), -w.y);
    float s = p.x * e.y - p.y * e.x;
    p = (s < 0.0) ? -p : p;
    vec2 v = p - vec2(wi, 0);
    v -= e * clamp(dot(v, e) / dot(e, e), -1.0, 1.0);
    d = min(d, vec2(dot(v, v), wi * he - abs(s)));
    return sqrt(d.x) * sign(-d.y);
}

// Quadratic Bezier (exact)
float sd2Bezier(vec2 pos, vec2 A, vec2 B, vec2 C) {
    vec2 a = B - A;
    vec2 b = A - 2.0 * B + C;
    vec2 c = a * 2.0;
    vec2 d = A - pos;
    float kk = 1.0 / dot(b, b);
    float kx = kk * dot(a, b);
    float ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
    float kz = kk * dot(d, a);
    float res = 0.0;
    float p = ky - kx * kx;
    float p3 = p * p * p;
    float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
    float h = q * q + 4.0 * p3;
    if (h >= 0.0) {
        h = sqrt(h);
        vec2 x = (vec2(h, -h) - q) / 2.0;
        vec2 uv = sign(x) * pow(abs(x), vec2(1.0 / 3.0));
        float t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
        res = dot2(d + (c + b * t) * t);
    } else {
        float z = sqrt(-p);
        float v = acos(q / (p * z * 2.0)) / 3.0;
        float m = cos(v);
        float n = sin(v) * 1.732050808;
        vec3 t = clamp(vec3(m + m, -n - m, n - m) * z - kx, 0.0, 1.0);
        res = min(dot2(d + (c + b * t.x) * t.x), dot2(d + (c + b * t.y) * t.y));
    }
    return sqrt(res);
}

// Uneven Capsule (exact)
float sd2UnevenCapsule(vec2 p, float r1, float r2, float h) {
    p.x = abs(p.x);
    float b = (r1 - r2) / h;
    float a = sqrt(1.0 - b * b);
    float k = dot(p, vec2(-b, a));
    if (k < 0.0) return length(p) - r1;
    if (k > a * h) return length(p - vec2(0.0, h)) - r2;
    return dot(p, vec2(a, b)) - r1;
}

// ============================================================================
// SECTION 4: 3D SDF PRIMITIVES
// ============================================================================

// Sphere (exact)
float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

// Box (exact)
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// Round Box - simple version (matches original hand_cigarette.comp)
float sdRoundBox(vec3 p, vec3 b, float r) {
    return sdBox(p, b) - r;
}

// Round Box - IQ's exact version (for reference)
float sdRoundBoxExact(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

// Box Frame (exact)
float sdBoxFrame(vec3 p, vec3 b, float e) {
    p = abs(p) - b;
    vec3 q = abs(p + e) - e;
    return min(min(
        length(max(vec3(p.x, q.y, q.z), 0.0)) + min(max(p.x, max(q.y, q.z)), 0.0),
        length(max(vec3(q.x, p.y, q.z), 0.0)) + min(max(q.x, max(p.y, q.z)), 0.0)),
        length(max(vec3(q.x, q.y, p.z), 0.0)) + min(max(q.x, max(q.y, p.z)), 0.0));
}

// Ellipsoid (bound)
float sdEllipsoid(vec3 p, vec3 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}

// Torus (exact)
float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

// Capped Torus (exact)
float sdCappedTorus(vec3 p, vec2 sc, float ra, float rb) {
    p.x = abs(p.x);
    float k = (sc.y * p.x > sc.x * p.y) ? dot(p.xy, sc) : length(p.xy);
    return sqrt(dot(p, p) + ra * ra - 2.0 * ra * k) - rb;
}

// Link (exact)
float sdLink(vec3 p, float le, float r1, float r2) {
    vec3 q = vec3(p.x, max(abs(p.y) - le, 0.0), p.z);
    return length(vec2(length(q.xy) - r1, q.z)) - r2;
}

// Capsule / Line Segment (exact)
float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// Vertical Capsule (exact)
float sdVerticalCapsule(vec3 p, float h, float r) {
    p.y -= clamp(p.y, 0.0, h);
    return length(p) - r;
}

// Cylinder (exact)
float sdCylinder(vec3 p, float h, float r) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Cylinder - Arbitrary (exact)
float sdCylinderArb(vec3 p, vec3 a, vec3 b, float r) {
    vec3 ba = b - a;
    vec3 pa = p - a;
    float baba = dot(ba, ba);
    float paba = dot(pa, ba);
    float x = length(pa * baba - ba * paba) - r * baba;
    float y = abs(paba - baba * 0.5) - baba * 0.5;
    float x2 = x * x;
    float y2 = y * y * baba;
    float d = (max(x, y) < 0.0) ? -min(x2, y2) : (((x > 0.0) ? x2 : 0.0) + ((y > 0.0) ? y2 : 0.0));
    return sign(d) * sqrt(abs(d)) / baba;
}

// Rounded Cylinder (exact)
float sdRoundedCylinder(vec3 p, float ra, float rb, float h) {
    vec2 d = vec2(length(p.xz) - ra + rb, abs(p.y) - h + rb);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rb;
}

// Cone (exact)
float sdCone(vec3 p, vec2 c, float h) {
    vec2 q = h * vec2(c.x / c.y, -1.0);
    vec2 w = vec2(length(p.xz), p.y);
    vec2 a = w - q * clamp(dot(w, q) / dot(q, q), 0.0, 1.0);
    vec2 b = w - q * vec2(clamp(w.x / q.x, 0.0, 1.0), 1.0);
    float k = sign(q.y);
    float d = min(dot(a, a), dot(b, b));
    float s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

// Infinite Cone (exact)
float sdConeInfinite(vec3 p, vec2 c) {
    vec2 q = vec2(length(p.xz), -p.y);
    float d = length(q - c * max(dot(q, c), 0.0));
    return d * ((q.x * c.y - q.y * c.x < 0.0) ? -1.0 : 1.0);
}

// Capped Cone (exact)
float sdCappedCone(vec3 p, float h, float r1, float r2) {
    vec2 q = vec2(length(p.xz), p.y);
    vec2 k1 = vec2(r2, h);
    vec2 k2 = vec2(r2 - r1, 2.0 * h);
    vec2 ca = vec2(q.x - min(q.x, (q.y < 0.0) ? r1 : r2), abs(q.y) - h);
    vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot2(k2), 0.0, 1.0);
    float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
    return s * sqrt(min(dot2(ca), dot2(cb)));
}

// Capped Cone - Arbitrary (exact)
float sdCappedConeArb(vec3 p, vec3 a, vec3 b, float ra, float rb) {
    float rba = rb - ra;
    float baba = dot(b - a, b - a);
    float papa = dot(p - a, p - a);
    float paba = dot(p - a, b - a) / baba;
    float x = sqrt(papa - paba * paba * baba);
    float cax = max(0.0, x - ((paba < 0.5) ? ra : rb));
    float cay = abs(paba - 0.5) - 0.5;
    float k = rba * rba + baba;
    float f = clamp((rba * (x - ra) + paba * baba) / k, 0.0, 1.0);
    float cbx = x - ra - f * rba;
    float cby = paba - f;
    float s = (cbx < 0.0 && cay < 0.0) ? -1.0 : 1.0;
    return s * sqrt(min(cax * cax + cay * cay * baba, cbx * cbx + cby * cby * baba));
}

// Round Cone (exact)
float sdRoundCone(vec3 p, float r1, float r2, float h) {
    float b = (r1 - r2) / h;
    float a = sqrt(1.0 - b * b);
    vec2 q = vec2(length(p.xz), p.y);
    float k = dot(q, vec2(-b, a));
    if (k < 0.0) return length(q) - r1;
    if (k > a * h) return length(q - vec2(0.0, h)) - r2;
    return dot(q, vec2(a, b)) - r1;
}

// Round Cone - Arbitrary (exact)
float sdRoundConeArb(vec3 p, vec3 a, vec3 b, float r1, float r2) {
    vec3 ba = b - a;
    float l2 = dot(ba, ba);
    float rr = r1 - r2;
    float a2 = l2 - rr * rr;
    float il2 = 1.0 / l2;
    vec3 pa = p - a;
    float y = dot(pa, ba);
    float z = y - l2;
    float x2 = dot2(pa * l2 - ba * y);
    float y2 = y * y * l2;
    float z2 = z * z * l2;
    float k = sign(rr) * rr * rr * x2;
    if (sign(z) * a2 * z2 > k) return sqrt(x2 + z2) * il2 - r2;
    if (sign(y) * a2 * y2 < k) return sqrt(x2 + y2) * il2 - r1;
    return (sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}

// Solid Angle (exact)
float sdSolidAngle(vec3 p, vec2 c, float ra) {
    vec2 q = vec2(length(p.xz), p.y);
    float l = length(q) - ra;
    float m = length(q - c * clamp(dot(q, c), 0.0, ra));
    return max(l, m * sign(c.y * q.x - c.x * q.y));
}

// Cut Sphere (exact)
float sdCutSphere(vec3 p, float r, float h) {
    float w = sqrt(r * r - h * h);
    vec2 q = vec2(length(p.xz), p.y);
    float s = max((h - r) * q.x * q.x + w * w * (h + r - 2.0 * q.y), h * q.x - w * q.y);
    return (s < 0.0) ? length(q) - r : (q.x < w) ? h - q.y : length(q - vec2(w, h));
}

// Cut Hollow Sphere (exact)
float sdCutHollowSphere(vec3 p, float r, float h, float t) {
    float w = sqrt(r * r - h * h);
    vec2 q = vec2(length(p.xz), p.y);
    return ((h * q.x < w * q.y) ? length(q - vec2(w, h)) : abs(length(q) - r)) - t;
}

// Death Star (exact)
float sdDeathStar(vec3 p2, float ra, float rb, float d) {
    float a = (ra * ra - rb * rb + d * d) / (2.0 * d);
    float b = sqrt(max(ra * ra - a * a, 0.0));
    vec2 p = vec2(p2.x, length(p2.yz));
    if (p.x * b - p.y * a > d * max(b - p.y, 0.0)) return length(p - vec2(a, b));
    else return max((length(p) - ra), -(length(p - vec2(d, 0.0)) - rb));
}

// Plane (exact)
float sdPlane(vec3 p, vec3 n, float h) {
    return dot(p, n) + h;
}

// Hexagonal Prism (exact)
float sdHexPrism(vec3 p, vec2 h) {
    const vec3 k = vec3(-0.8660254, 0.5, 0.57735);
    p = abs(p);
    p.xy -= 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy;
    vec2 d = vec2(length(p.xy - vec2(clamp(p.x, -k.z * h.x, k.z * h.x), h.x)) * sign(p.y - h.x), p.z - h.y);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Triangular Prism (bound)
float sdTriPrism(vec3 p, vec2 h) {
    vec3 q = abs(p);
    return max(q.z - h.y, max(q.x * 0.866025 + p.y * 0.5, -p.y) - h.x * 0.5);
}

// Octahedron (exact)
float sdOctahedron(vec3 p, float s) {
    p = abs(p);
    float m = p.x + p.y + p.z - s;
    vec3 q;
    if (3.0 * p.x < m) q = p.xyz;
    else if (3.0 * p.y < m) q = p.yzx;
    else if (3.0 * p.z < m) q = p.zxy;
    else return m * 0.57735027;
    float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
    return length(vec3(q.x, q.y - s + k, q.z - k));
}

// Octahedron (bound, fast)
float sdOctahedronBound(vec3 p, float s) {
    p = abs(p);
    return (p.x + p.y + p.z - s) * 0.57735027;
}

// Pyramid (exact)
float sdPyramid(vec3 p, float h) {
    float m2 = h * h + 0.25;
    p.xz = abs(p.xz);
    p.xz = (p.z > p.x) ? p.zx : p.xz;
    p.xz -= 0.5;
    vec3 q = vec3(p.z, h * p.y - 0.5 * p.x, h * p.x + 0.5 * p.y);
    float s = max(-q.x, 0.0);
    float t = clamp((q.y - 0.5 * p.z) / (m2 + 0.25), 0.0, 1.0);
    float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
    float b = m2 * (q.x + 0.5 * t) * (q.x + 0.5 * t) + (q.y - m2 * t) * (q.y - m2 * t);
    float d2 = min(q.y, -q.x * m2 - q.y * 0.5) > 0.0 ? 0.0 : min(a, b);
    return sqrt((d2 + q.z * q.z) / m2) * sign(max(q.z, -p.y));
}

// Rhombus (exact)
float sdRhombus(vec3 p, float la, float lb, float h, float ra) {
    p = abs(p);
    float f = clamp((la * p.x - lb * p.z + lb * lb) / (la * la + lb * lb), 0.0, 1.0);
    vec2 w = p.xz - vec2(la, lb) * vec2(f, 1.0 - f);
    vec2 q = vec2(length(w) * sign(w.x) - ra, p.y - h);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0));
}

// Triangle (exact)
float udTriangle(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ba = b - a, pa = p - a;
    vec3 cb = c - b, pb = p - b;
    vec3 ac = a - c, pc = p - c;
    vec3 nor = cross(ba, ac);
    return sqrt(
        (sign(dot(cross(ba, nor), pa)) +
         sign(dot(cross(cb, nor), pb)) +
         sign(dot(cross(ac, nor), pc)) < 2.0)
        ? min(min(
            dot2(ba * clamp(dot(ba, pa) / dot2(ba), 0.0, 1.0) - pa),
            dot2(cb * clamp(dot(cb, pb) / dot2(cb), 0.0, 1.0) - pb)),
            dot2(ac * clamp(dot(ac, pc) / dot2(ac), 0.0, 1.0) - pc))
        : dot(nor, pa) * dot(nor, pa) / dot2(nor));
}

// Quad (exact)
float udQuad(vec3 p, vec3 a, vec3 b, vec3 c, vec3 d) {
    vec3 ba = b - a, pa = p - a;
    vec3 cb = c - b, pb = p - b;
    vec3 dc = d - c, pc = p - c;
    vec3 ad = a - d, pd = p - d;
    vec3 nor = cross(ba, ad);
    return sqrt(
        (sign(dot(cross(ba, nor), pa)) +
         sign(dot(cross(cb, nor), pb)) +
         sign(dot(cross(dc, nor), pc)) +
         sign(dot(cross(ad, nor), pd)) < 3.0)
        ? min(min(min(
            dot2(ba * clamp(dot(ba, pa) / dot2(ba), 0.0, 1.0) - pa),
            dot2(cb * clamp(dot(cb, pb) / dot2(cb), 0.0, 1.0) - pb)),
            dot2(dc * clamp(dot(dc, pc) / dot2(dc), 0.0, 1.0) - pc)),
            dot2(ad * clamp(dot(ad, pd) / dot2(ad), 0.0, 1.0) - pd))
        : dot(nor, pa) * dot(nor, pa) / dot2(nor));
}

// ============================================================================
// SECTION 5: BOOLEAN OPERATIONS
// ============================================================================

// Union (exact)
float opUnion(float d1, float d2) {
    return min(d1, d2);
}

// Subtraction (bound)
float opSubtract(float d1, float d2) {
    return max(-d1, d2);
}

// Intersection (bound)
float opIntersect(float d1, float d2) {
    return max(d1, d2);
}

// XOR (exact)
float opXor(float d1, float d2) {
    return max(min(d1, d2), -max(d1, d2));
}

// ============================================================================
// SECTION 6: SMOOTH BLENDING (smin/smax family)
// ============================================================================

// --- Smooth Minimum Variants ---

// Polynomial (Quadratic) - Fast, rigid, recommended default
float sminQuadratic(float a, float b, float k) {
    k *= 4.0;
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

// Polynomial (Cubic) - Smoother transition
float sminCubic(float a, float b, float k) {
    k *= 6.0;
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * h * k * (1.0 / 6.0);
}

// Polynomial (Quartic) - Even smoother
float sminQuartic(float a, float b, float k) {
    k *= 16.0 / 3.0;
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * h * (4.0 - h) * k * (1.0 / 16.0);
}

// Exponential - Non-rigid, extends beyond blend region
float sminExponential(float a, float b, float k) {
    float r = exp2(-a / k) + exp2(-b / k);
    return -k * log2(r);
}

// Square Root - Lacks rigidity
float sminRoot(float a, float b, float k) {
    k *= 2.0;
    float x = b - a;
    return 0.5 * (a + b - sqrt(x * x + k * k));
}

// Circular - Perfect circular blend profile
float sminCircular(float a, float b, float k) {
    k *= 1.0 / (1.0 - sqrt(0.5));
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - k * 0.5 * (1.0 + h - sqrt(1.0 - h * (h - 2.0)));
}

// Circular Geometrical - Associative
float sminCircularGeom(float a, float b, float k) {
    k *= 1.0 / (1.0 - sqrt(0.5));
    return max(k, min(a, b)) - length(max(k - vec2(a, b), vec2(0.0)));
}

// --- Smooth Maximum Variants ---

float smaxQuadratic(float a, float b, float k) {
    return -sminQuadratic(-a, -b, k);
}

float smaxCubic(float a, float b, float k) {
    return -sminCubic(-a, -b, k);
}

float smaxCircular(float a, float b, float k) {
    return -sminCircular(-a, -b, k);
}

// --- Smooth Operations (Classic formulas - matches original implementations) ---

// Smooth Union - CLASSIC formula (preserves original behavior)
float opSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

// Smooth Subtraction - CLASSIC formula
float opSmoothSubtract(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 + d1) / k, 0.0, 1.0);
    return mix(d2, -d1, h) + k * h * (1.0 - h);
}

// Smooth Intersection
float opSmoothIntersect(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) + k * h * (1.0 - h);
}

// --- Smooth Operations (Normalized - newer IQ formulas for reference) ---

float opSmoothUnionNormalized(float d1, float d2, float k) {
    return sminQuadratic(d1, d2, k);
}

float opSmoothSubtractNormalized(float d1, float d2, float k) {
    return smaxQuadratic(-d1, d2, k);
}

// --- Material Blending Variants (returns vec2: x=distance, y=blend factor) ---

vec2 opSmoothUnionMat(float d1, float d2, float k) {
    float h = 1.0 - min(abs(d1 - d2) / (4.0 * k), 1.0);
    float w = h * h;
    float m = w * 0.5;
    float s = w * k;
    return (d1 < d2) ? vec2(d1 - s, m) : vec2(d2 - s, 1.0 - m);
}

vec2 opSmoothUnionMatCubic(float d1, float d2, float k) {
    float h = 1.0 - min(abs(d1 - d2) / (6.0 * k), 1.0);
    float w = h * h * h;
    float m = w * 0.5;
    float s = w * k;
    return (d1 < d2) ? vec2(d1 - s, m) : vec2(d2 - s, 1.0 - m);
}

// ============================================================================
// SECTION 7: DOMAIN TRANSFORMATIONS
// ============================================================================

// Elongation - stretch shape along axes (exact)
vec3 opElongate(vec3 p, vec3 h) {
    return p - clamp(p, -h, h);
}

// Alternative elongation (exact interior)
vec4 opElongateAlt(vec3 p, vec3 h) {
    vec3 q = abs(p) - h;
    return vec4(max(q, 0.0), min(max(q.x, max(q.y, q.z)), 0.0));
}

// Rounding - add radius to any shape (exact)
float opRound(float d, float r) {
    return d - r;
}

// Onion / Hollowing - make shell of any shape (exact)
float opOnion(float d, float thickness) {
    return abs(d) - thickness;
}

// Uniform Scale (exact)
float opScale(float d, float s) {
    return d * s;
}

// ============================================================================
// SECTION 8: DOMAIN REPETITION
// ============================================================================

// Infinite Repetition - use for symmetric shapes only
vec3 opRepeat(vec3 p, vec3 spacing) {
    return p - spacing * round(p / spacing);
}

// Limited/Finite Repetition
vec3 opRepeatLimited(vec3 p, float spacing, vec3 limit) {
    return p - spacing * clamp(round(p / spacing), -limit, limit);
}

// 1D Repetition along X
float opRepeat1D(inout float p, float spacing) {
    float id = round(p / spacing);
    p = p - spacing * id;
    return id;
}

// Limited 1D Repetition
float opRepeat1DLimited(inout float p, float spacing, float limit) {
    float id = clamp(round(p / spacing), -limit, limit);
    p = p - spacing * id;
    return id;
}

// Polar/Angular Repetition (2D)
vec2 opRepeatPolar(vec2 p, int n) {
    float angle = 6.283185 / float(n);
    float a = atan(p.y, p.x) + angle * 0.5;
    a = mod(a, angle) - angle * 0.5;
    return length(p) * vec2(cos(a), sin(a));
}

// Polar Repetition returning cell ID
vec2 opRepeatPolarId(vec2 p, int n, out float id) {
    float angle = 6.283185 / float(n);
    float a = atan(p.y, p.x) + angle * 0.5;
    id = floor(a / angle);
    a = mod(a, angle) - angle * 0.5;
    return length(p) * vec2(cos(a), sin(a));
}

// Symmetry
vec3 opSymX(vec3 p) { p.x = abs(p.x); return p; }
vec3 opSymXY(vec3 p) { p.xy = abs(p.xy); return p; }
vec3 opSymXZ(vec3 p) { p.xz = abs(p.xz); return p; }
vec3 opSymXYZ(vec3 p) { return abs(p); }

// ============================================================================
// SECTION 9: DEFORMATIONS
// ============================================================================

// Twist around Y axis (bound - use conservative step)
vec3 opTwist(vec3 p, float k) {
    float c = cos(k * p.y);
    float s = sin(k * p.y);
    mat2 m = mat2(c, -s, s, c);
    return vec3(m * p.xz, p.y);
}

// Bend around X axis (bound)
vec3 opBend(vec3 p, float k) {
    float c = cos(k * p.x);
    float s = sin(k * p.x);
    mat2 m = mat2(c, -s, s, c);
    return vec3(m * p.xy, p.z);
}

// Taper along Y axis
vec3 opTaper(vec3 p, float amount) {
    float scale = 1.0 + p.y * amount;
    return vec3(p.x / scale, p.y, p.z / scale);
}

// Displacement (add to distance, bound)
float opDisplace(float d, vec3 p, float amplitude, float frequency) {
    float disp = sin(p.x * frequency) * sin(p.y * frequency) * sin(p.z * frequency);
    return d + disp * amplitude;
}

// ============================================================================
// SECTION 10: 2D→3D OPERATIONS
// ============================================================================

// Extrusion - extrude 2D shape along Z (exact if 2D is exact)
float opExtrude(float d2d, float pz, float h) {
    vec2 w = vec2(d2d, abs(pz) - h);
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}

// Revolution - revolve 2D shape around Y (exact if 2D is exact)
// Note: sd2D should be called with vec2(length(p.xz) - offset, p.y)
float opRevolution(vec3 p, float offset) {
    return length(p.xz) - offset;  // Returns q.x for the 2D SDF
}

// ============================================================================
// SECTION 11: NOISE & PROCEDURAL
// ============================================================================

// Hash functions
float sdfHash11(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

float sdfHash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float sdfHash31(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

vec3 sdfHash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

// Value Noise 2D
float sdfValueNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // Smoothstep

    float a = sdfHash21(i);
    float b = sdfHash21(i + vec2(1.0, 0.0));
    float c = sdfHash21(i + vec2(0.0, 1.0));
    float d = sdfHash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Value Noise 3D
float sdfValueNoise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n = mix(
        mix(mix(sdfHash31(i), sdfHash31(i + vec3(1, 0, 0)), f.x),
            mix(sdfHash31(i + vec3(0, 1, 0)), sdfHash31(i + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(sdfHash31(i + vec3(0, 0, 1)), sdfHash31(i + vec3(1, 0, 1)), f.x),
            mix(sdfHash31(i + vec3(0, 1, 1)), sdfHash31(i + vec3(1, 1, 1)), f.x), f.y),
        f.z
    );
    return n;
}

// FBM 2D (Fractal Brownian Motion)
float sdfFbm2D(vec2 p, int octaves, float lacunarity, float gain) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * sdfValueNoise2D(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

// FBM 3D
float sdfFbm3D(vec3 p, int octaves, float lacunarity, float gain) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * sdfValueNoise3D(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

// Simplified FBM (3 octaves, default params)
float sdfFbm3DSimple(vec3 p) {
    float f = 0.0;
    f += 0.5000 * sdfValueNoise3D(p); p *= 2.0;
    f += 0.2500 * sdfValueNoise3D(p); p *= 2.0;
    f += 0.1250 * sdfValueNoise3D(p);
    return f;
}

// ============================================================================
// SECTION 12: ORGANIC DETAIL (FBM for SDFs)
// ============================================================================

// Base SDF for FBM - grid of random spheres
float sdfFbmBaseSphere(vec3 p) {
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);

    float d = 1e10;
    for (int z = 0; z <= 1; z++)
    for (int y = 0; y <= 1; y++)
    for (int x = 0; x <= 1; x++) {
        ivec3 c = ivec3(x, y, z);
        float rad = 0.5 * sdfHash31(vec3(i + c));
        d = min(d, length(f - vec3(c)) - rad);
    }
    return d;
}

// Add FBM detail to an SDF (use on already-computed distance)
float sdfAddFbmDetail(vec3 p, float d, int octaves, float amplitude) {
    float s = 1.0;

    // Rotation matrix for breaking grid alignment
    mat3 m = mat3(0.00, 1.60, 1.20,
                  -1.60, 0.72, -0.96,
                  -1.20, -0.96, 1.28);

    for (int i = 0; i < octaves; i++) {
        // Create noise layer
        float n = s * sdfFbmBaseSphere(p);

        // Clip to surface vicinity (smooth intersection)
        n = smaxQuadratic(n, d - 0.1 * s * amplitude, 0.3 * s * amplitude);

        // Merge with smooth union
        d = sminQuadratic(n, d, 0.3 * s * amplitude);

        // Transform for next octave
        p = m * p;
        s *= 0.5;
    }
    return d;
}

// ============================================================================
// SECTION 13: LIGHTING UTILITIES
// ============================================================================
// Note: Functions requiring a scene SDF callback are implemented as macros
// or must be defined in the shader itself since GLSL doesn't support
// function pointers. See hand_cigarette.comp for examples of:
//   - calcSoftShadow()
//   - calcAO()
//
// These utility functions below don't require callbacks:

// Fresnel approximation (Schlick)
float sdfFresnel(vec3 viewDir, vec3 normal, float f0) {
    float cosTheta = max(dot(viewDir, normal), 0.0);
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

// Simple rim lighting factor
float sdfRimLight(vec3 viewDir, vec3 normal, float power) {
    return pow(1.0 - max(dot(viewDir, normal), 0.0), power);
}

// Hemisphere ambient (sky/ground blend)
vec3 sdfHemisphereAmbient(vec3 normal, vec3 skyColor, vec3 groundColor) {
    float blend = normal.y * 0.5 + 0.5;
    return mix(groundColor, skyColor, blend);
}

// Simple subsurface scattering approximation (no callback needed)
// thickness = pre-computed or estimated material thickness
float sdfSubsurfaceScatter(float thickness, float absorption) {
    return exp(-thickness * absorption);
}

// ============================================================================
// SECTION 14: ANIMATION HELPERS
// ============================================================================

// Smooth morphing between two SDFs
float opMorph(float d1, float d2, float t) {
    return mix(d1, d2, smoothstep(0.0, 1.0, t));
}

// Pulsing scale
float opPulse(float d, float time, float amount, float speed) {
    float pulse = 1.0 + sin(time * speed) * amount;
    return d / pulse;
}

// Wave displacement
float opWave(float d, vec3 p, float time, float amplitude, float frequency) {
    float wave = sin(p.x * frequency + time) *
                 sin(p.y * frequency + time * 0.7) *
                 sin(p.z * frequency + time * 0.5);
    return d + wave * amplitude;
}

// Breathing motion
vec3 opBreathe(vec3 p, float time, float amount) {
    float breath = 1.0 + sin(time * 0.5) * amount;
    return p / breath;
}

#endif // SDF_LIB_GLSL
