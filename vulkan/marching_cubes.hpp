// Marching Cubes - CPU implementation for sampled SDF grids
// Works with GPU-sampled distance fields
//
// Features:
//   - Parallel processing (uses std::thread)
//   - Optional vertex colors (from color sampling)
//   - Optional UV coordinates (triplanar mapping)
//   - Automatic normal computation
//
// Usage:
//   std::vector<float> distances = sample_sdf_from_gpu(...);
//   auto mesh = mc::generateMesh(distances, resolution, bounds_min, bounds_max);
//   mc::exportOBJ("output.obj", mesh);  // Basic export
//   mc::exportOBJ("output.obj", mesh, true, true);  // With colors and UVs

#pragma once

#include <vector>
#include <array>
#include <fstream>
#include <cmath>
#include <cfloat>
#include <locale>
#include <thread>
#include <mutex>
#include <atomic>

// tinygltf for GLB export with vertex colors
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tinygltf/tiny_gltf.h"

namespace mc {

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 normalized() const {
        float len = length();
        if (len < 0.00001f) return {0, 1, 0};
        return *this * (1.0f / len);
    }
};

struct Vec2 {
    float u, v;
    Vec2() : u(0), v(0) {}
    Vec2(float u_, float v_) : u(u_), v(v_) {}
};

struct Color3 {
    float r, g, b;
    Color3() : r(0.8f), g(0.8f), b(0.8f) {}
    Color3(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
};

// Extended vertex with all attributes
struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Color3 color;
    Vec2 uv;
};

struct Triangle {
    Vec3 v[3];
};

// Mesh with extended vertex data
struct Mesh {
    std::vector<Vec3> vertices;      // Positions (always filled)
    std::vector<Vec3> normals;       // Per-vertex normals (computed automatically)
    std::vector<Color3> colors;      // Per-vertex colors (optional, from color sampling)
    std::vector<Vec2> uvs;           // Per-vertex UVs (optional, triplanar mapping)
    std::vector<uint32_t> indices;

    bool hasColors() const { return !colors.empty() && colors.size() == vertices.size(); }
    bool hasUVs() const { return !uvs.empty() && uvs.size() == vertices.size(); }
    bool hasNormals() const { return !normals.empty() && normals.size() == vertices.size(); }
};

// Marching Cubes lookup tables
// Edge table: for each cube configuration, which edges are intersected
static const int edgeTable[256] = {
    0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
};

// Triangle table: for each cube configuration, list of triangle vertices (edge indices)
static const int triTable[256][16] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
    {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
    {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
    {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
    {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
    {5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
    {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
    {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
    {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
    {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
    {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
    {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
    {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
    {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
    {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
    {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
    {7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
    {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
    {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
    {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
    {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
    {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
    {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
    {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
    {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
    {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
    {9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
    {8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
    {3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
    {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
    {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
    {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
    {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
    {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
    {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
    {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
    {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
    {7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
    {10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
    {0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
    {7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
    {9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
    {6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
    {4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
    {10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
    {8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
    {1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
    {10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
    {10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
    {9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
    {7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
    {3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
    {7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
    {3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
    {6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
    {9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
    {1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
    {4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
    {7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
    {6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
    {0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
    {6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
    {11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
    {6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
    {5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
    {9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
    {1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
    {10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
    {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
    {11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
    {9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
    {7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
    {2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
    {9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
    {1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
    {10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
    {2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
    {0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
    {0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
    {9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
    {5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
    {5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
    {9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
    {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
    {3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
    {4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
    {9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
    {11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
    {2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
    {9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
    {3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
    {1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
    {4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
    {0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
    {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
};

// Interpolate vertex along edge
inline Vec3 vertexInterp(float isolevel, const Vec3& p1, const Vec3& p2, float v1, float v2) {
    if (std::abs(isolevel - v1) < 0.00001f) return p1;
    if (std::abs(isolevel - v2) < 0.00001f) return p2;
    if (std::abs(v1 - v2) < 0.00001f) return p1;
    float mu = (isolevel - v1) / (v2 - v1);
    return p1 + (p2 - p1) * mu;
}

// Compute triplanar UV from position and normal
inline Vec2 triplanarUV(const Vec3& pos, const Vec3& normal, float scale = 10.0f) {
    Vec3 absN = {std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};

    // Blend weights based on normal
    float u, v;
    if (absN.x >= absN.y && absN.x >= absN.z) {
        // X-axis dominant - project onto YZ
        u = pos.y * scale;
        v = pos.z * scale;
    } else if (absN.y >= absN.x && absN.y >= absN.z) {
        // Y-axis dominant - project onto XZ
        u = pos.x * scale;
        v = pos.z * scale;
    } else {
        // Z-axis dominant - project onto XY
        u = pos.x * scale;
        v = pos.y * scale;
    }

    // Wrap to [0,1]
    u = u - std::floor(u);
    v = v - std::floor(v);

    return Vec2(u, v);
}

// Thread-local mesh buffer for parallel processing
struct ThreadMesh {
    std::vector<Vec3> vertices;
    std::vector<uint32_t> indices;
};

// Generate mesh from a 3D grid of SDF values (PARALLEL VERSION)
// distances: flattened 3D array of size res*res*res (x varies fastest)
// res: grid resolution in each dimension
// bounds_min, bounds_max: world-space bounds
inline Mesh generateMesh(
    const std::vector<float>& distances,
    int res,
    Vec3 bounds_min,
    Vec3 bounds_max,
    float isolevel = 0.0f
) {
    Vec3 cell_size = {
        (bounds_max.x - bounds_min.x) / (res - 1),
        (bounds_max.y - bounds_min.y) / (res - 1),
        (bounds_max.z - bounds_min.z) / (res - 1)
    };

    auto idx = [res](int x, int y, int z) { return x + y * res + z * res * res; };

    // Determine number of threads
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;  // Fallback
    numThreads = std::min(numThreads, (unsigned int)(res - 1));

    // Per-thread mesh buffers
    std::vector<ThreadMesh> threadMeshes(numThreads);

    // Parallel processing - each thread handles a range of Z slices
    std::vector<std::thread> threads;
    int zPerThread = (res - 1 + numThreads - 1) / numThreads;

    for (unsigned int t = 0; t < numThreads; t++) {
        int zStart = t * zPerThread;
        int zEnd = std::min(zStart + zPerThread, res - 1);

        threads.emplace_back([&, t, zStart, zEnd]() {
            ThreadMesh& tm = threadMeshes[t];
            Vec3 vertList[12];

            for (int z = zStart; z < zEnd; z++) {
                for (int y = 0; y < res - 1; y++) {
                    for (int x = 0; x < res - 1; x++) {
                        // Get 8 corner values
                        float v[8] = {
                            distances[idx(x,   y,   z)],
                            distances[idx(x+1, y,   z)],
                            distances[idx(x+1, y+1, z)],
                            distances[idx(x,   y+1, z)],
                            distances[idx(x,   y,   z+1)],
                            distances[idx(x+1, y,   z+1)],
                            distances[idx(x+1, y+1, z+1)],
                            distances[idx(x,   y+1, z+1)]
                        };

                        // Get 8 corner positions
                        Vec3 p[8] = {
                            {bounds_min.x + x * cell_size.x,     bounds_min.y + y * cell_size.y,     bounds_min.z + z * cell_size.z},
                            {bounds_min.x + (x+1) * cell_size.x, bounds_min.y + y * cell_size.y,     bounds_min.z + z * cell_size.z},
                            {bounds_min.x + (x+1) * cell_size.x, bounds_min.y + (y+1) * cell_size.y, bounds_min.z + z * cell_size.z},
                            {bounds_min.x + x * cell_size.x,     bounds_min.y + (y+1) * cell_size.y, bounds_min.z + z * cell_size.z},
                            {bounds_min.x + x * cell_size.x,     bounds_min.y + y * cell_size.y,     bounds_min.z + (z+1) * cell_size.z},
                            {bounds_min.x + (x+1) * cell_size.x, bounds_min.y + y * cell_size.y,     bounds_min.z + (z+1) * cell_size.z},
                            {bounds_min.x + (x+1) * cell_size.x, bounds_min.y + (y+1) * cell_size.y, bounds_min.z + (z+1) * cell_size.z},
                            {bounds_min.x + x * cell_size.x,     bounds_min.y + (y+1) * cell_size.y, bounds_min.z + (z+1) * cell_size.z}
                        };

                        // Determine cube index
                        int cubeIndex = 0;
                        if (v[0] < isolevel) cubeIndex |= 1;
                        if (v[1] < isolevel) cubeIndex |= 2;
                        if (v[2] < isolevel) cubeIndex |= 4;
                        if (v[3] < isolevel) cubeIndex |= 8;
                        if (v[4] < isolevel) cubeIndex |= 16;
                        if (v[5] < isolevel) cubeIndex |= 32;
                        if (v[6] < isolevel) cubeIndex |= 64;
                        if (v[7] < isolevel) cubeIndex |= 128;

                        // Skip if completely inside or outside
                        if (edgeTable[cubeIndex] == 0) continue;

                        // Interpolate vertices on edges
                        if (edgeTable[cubeIndex] & 1)    vertList[0]  = vertexInterp(isolevel, p[0], p[1], v[0], v[1]);
                        if (edgeTable[cubeIndex] & 2)    vertList[1]  = vertexInterp(isolevel, p[1], p[2], v[1], v[2]);
                        if (edgeTable[cubeIndex] & 4)    vertList[2]  = vertexInterp(isolevel, p[2], p[3], v[2], v[3]);
                        if (edgeTable[cubeIndex] & 8)    vertList[3]  = vertexInterp(isolevel, p[3], p[0], v[3], v[0]);
                        if (edgeTable[cubeIndex] & 16)   vertList[4]  = vertexInterp(isolevel, p[4], p[5], v[4], v[5]);
                        if (edgeTable[cubeIndex] & 32)   vertList[5]  = vertexInterp(isolevel, p[5], p[6], v[5], v[6]);
                        if (edgeTable[cubeIndex] & 64)   vertList[6]  = vertexInterp(isolevel, p[6], p[7], v[6], v[7]);
                        if (edgeTable[cubeIndex] & 128)  vertList[7]  = vertexInterp(isolevel, p[7], p[4], v[7], v[4]);
                        if (edgeTable[cubeIndex] & 256)  vertList[8]  = vertexInterp(isolevel, p[0], p[4], v[0], v[4]);
                        if (edgeTable[cubeIndex] & 512)  vertList[9]  = vertexInterp(isolevel, p[1], p[5], v[1], v[5]);
                        if (edgeTable[cubeIndex] & 1024) vertList[10] = vertexInterp(isolevel, p[2], p[6], v[2], v[6]);
                        if (edgeTable[cubeIndex] & 2048) vertList[11] = vertexInterp(isolevel, p[3], p[7], v[3], v[7]);

                        // Add triangles
                        for (int i = 0; triTable[cubeIndex][i] != -1; i += 3) {
                            uint32_t baseIdx = (uint32_t)tm.vertices.size();
                            tm.vertices.push_back(vertList[triTable[cubeIndex][i]]);
                            tm.vertices.push_back(vertList[triTable[cubeIndex][i+1]]);
                            tm.vertices.push_back(vertList[triTable[cubeIndex][i+2]]);
                            tm.indices.push_back(baseIdx);
                            tm.indices.push_back(baseIdx + 1);
                            tm.indices.push_back(baseIdx + 2);
                        }
                    }
                }
            }
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    // Merge all thread meshes
    Mesh mesh;
    size_t totalVerts = 0, totalIndices = 0;
    for (const auto& tm : threadMeshes) {
        totalVerts += tm.vertices.size();
        totalIndices += tm.indices.size();
    }
    mesh.vertices.reserve(totalVerts);
    mesh.indices.reserve(totalIndices);

    uint32_t indexOffset = 0;
    for (const auto& tm : threadMeshes) {
        mesh.vertices.insert(mesh.vertices.end(), tm.vertices.begin(), tm.vertices.end());
        for (uint32_t idx : tm.indices) {
            mesh.indices.push_back(idx + indexOffset);
        }
        indexOffset += (uint32_t)tm.vertices.size();
    }

    return mesh;
}

// Compute normals from triangle geometry
inline void computeNormals(Mesh& mesh) {
    mesh.normals.resize(mesh.vertices.size(), Vec3(0, 0, 0));

    // Compute face normals and accumulate at vertices
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i+1];
        uint32_t i2 = mesh.indices[i+2];

        Vec3 v0 = mesh.vertices[i0];
        Vec3 v1 = mesh.vertices[i1];
        Vec3 v2 = mesh.vertices[i2];

        Vec3 e1 = v1 - v0;
        Vec3 e2 = v2 - v0;
        Vec3 normal = e1.cross(e2);

        mesh.normals[i0] = mesh.normals[i0] + normal;
        mesh.normals[i1] = mesh.normals[i1] + normal;
        mesh.normals[i2] = mesh.normals[i2] + normal;
    }

    // Normalize
    for (auto& n : mesh.normals) {
        n = n.normalized();
    }
}

// Compute triplanar UVs from positions and normals
inline void computeUVs(Mesh& mesh, float scale = 10.0f) {
    if (!mesh.hasNormals()) {
        computeNormals(mesh);
    }

    mesh.uvs.resize(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        mesh.uvs[i] = triplanarUV(mesh.vertices[i], mesh.normals[i], scale);
    }
}

// Set uniform vertex color
inline void setUniformColor(Mesh& mesh, float r, float g, float b) {
    mesh.colors.resize(mesh.vertices.size(), Color3(r, g, b));
}

// Set colors from external color array (e.g., from GPU sampling)
inline void setColors(Mesh& mesh, const std::vector<Color3>& colors) {
    if (colors.size() == mesh.vertices.size()) {
        mesh.colors = colors;
    }
}

// Export mesh to OBJ file with optional colors and UVs
inline bool exportOBJ(const std::string& filename, const Mesh& mesh,
                      bool includeColors = false, bool includeUVs = false) {
    if (mesh.vertices.empty()) return false;

    std::ofstream file(filename);
    if (!file.is_open()) return false;

    // Use C locale to avoid thousands separators in numbers
    file.imbue(std::locale::classic());

    file << "# Generated by Marching Cubes (Parallel)\n";
    file << "# Vertices: " << mesh.vertices.size() << "\n";
    file << "# Triangles: " << mesh.indices.size() / 3 << "\n";
    if (includeColors && mesh.hasColors()) file << "# With vertex colors\n";
    if (includeUVs && mesh.hasUVs()) file << "# With UV coordinates\n";
    file << "\n";

    // Write vertices (optionally with colors)
    bool writeColors = includeColors && mesh.hasColors();
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        const Vec3& v = mesh.vertices[i];
        if (writeColors) {
            const Color3& c = mesh.colors[i];
            file << "v " << v.x << " " << v.y << " " << v.z
                 << " " << c.r << " " << c.g << " " << c.b << "\n";
        } else {
            file << "v " << v.x << " " << v.y << " " << v.z << "\n";
        }
    }

    // Write texture coordinates
    if (includeUVs && mesh.hasUVs()) {
        file << "\n";
        for (const auto& uv : mesh.uvs) {
            file << "vt " << uv.u << " " << uv.v << "\n";
        }
    }

    // Write normals
    if (mesh.hasNormals()) {
        file << "\n";
        for (const auto& n : mesh.normals) {
            file << "vn " << n.x << " " << n.y << " " << n.z << "\n";
        }
    }

    // Write faces (winding order flipped for correct OBJ convention: CCW = front face)
    file << "\n";
    bool hasUV = includeUVs && mesh.hasUVs();
    bool hasN = mesh.hasNormals();

    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        // Flip winding: i0, i2, i1 instead of i0, i1, i2
        uint32_t i0 = mesh.indices[i] + 1;
        uint32_t i1 = mesh.indices[i+2] + 1;  // Swapped
        uint32_t i2 = mesh.indices[i+1] + 1;  // Swapped

        if (hasUV && hasN) {
            file << "f " << i0 << "/" << i0 << "/" << i0 << " "
                        << i1 << "/" << i1 << "/" << i1 << " "
                        << i2 << "/" << i2 << "/" << i2 << "\n";
        } else if (hasN) {
            file << "f " << i0 << "//" << i0 << " "
                        << i1 << "//" << i1 << " "
                        << i2 << "//" << i2 << "\n";
        } else if (hasUV) {
            file << "f " << i0 << "/" << i0 << " "
                        << i1 << "/" << i1 << " "
                        << i2 << "/" << i2 << "\n";
        } else {
            file << "f " << i0 << " " << i1 << " " << i2 << "\n";
        }
    }

    file.close();
    return true;
}

// Export mesh to GLB (binary GLTF) with proper vertex color support
inline bool exportGLB(const std::string& filename, const Mesh& mesh,
                      bool includeColors = true) {
    if (mesh.vertices.empty()) return false;

    tinygltf::Model model;
    tinygltf::Scene scene;
    tinygltf::Mesh gltfMesh;
    tinygltf::Primitive primitive;
    tinygltf::Buffer buffer;
    tinygltf::BufferView positionView, normalView, colorView, indexView;
    tinygltf::Accessor positionAccessor, normalAccessor, colorAccessor, indexAccessor;

    // Calculate buffer sizes
    size_t vertexCount = mesh.vertices.size();
    size_t indexCount = mesh.indices.size();
    size_t positionSize = vertexCount * 3 * sizeof(float);
    size_t normalSize = mesh.hasNormals() ? vertexCount * 3 * sizeof(float) : 0;
    size_t colorSize = (includeColors && mesh.hasColors()) ? vertexCount * 4 * sizeof(float) : 0;
    size_t indexSize = indexCount * sizeof(uint32_t);

    // Create buffer data
    size_t offset = 0;
    buffer.data.resize(positionSize + normalSize + colorSize + indexSize);

    // Positions
    std::vector<float> positions(vertexCount * 3);
    float minPos[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float maxPos[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (size_t i = 0; i < vertexCount; i++) {
        positions[i * 3 + 0] = mesh.vertices[i].x;
        positions[i * 3 + 1] = mesh.vertices[i].y;
        positions[i * 3 + 2] = mesh.vertices[i].z;
        minPos[0] = std::min(minPos[0], mesh.vertices[i].x);
        minPos[1] = std::min(minPos[1], mesh.vertices[i].y);
        minPos[2] = std::min(minPos[2], mesh.vertices[i].z);
        maxPos[0] = std::max(maxPos[0], mesh.vertices[i].x);
        maxPos[1] = std::max(maxPos[1], mesh.vertices[i].y);
        maxPos[2] = std::max(maxPos[2], mesh.vertices[i].z);
    }
    memcpy(buffer.data.data() + offset, positions.data(), positionSize);

    positionView.buffer = 0;
    positionView.byteOffset = offset;
    positionView.byteLength = positionSize;
    positionView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    offset += positionSize;

    positionAccessor.bufferView = 0;
    positionAccessor.byteOffset = 0;
    positionAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    positionAccessor.count = vertexCount;
    positionAccessor.type = TINYGLTF_TYPE_VEC3;
    positionAccessor.minValues = {minPos[0], minPos[1], minPos[2]};
    positionAccessor.maxValues = {maxPos[0], maxPos[1], maxPos[2]};

    // Normals
    int normalAccessorIdx = -1;
    if (mesh.hasNormals()) {
        std::vector<float> normals(vertexCount * 3);
        for (size_t i = 0; i < vertexCount; i++) {
            normals[i * 3 + 0] = mesh.normals[i].x;
            normals[i * 3 + 1] = mesh.normals[i].y;
            normals[i * 3 + 2] = mesh.normals[i].z;
        }
        memcpy(buffer.data.data() + offset, normals.data(), normalSize);

        normalView.buffer = 0;
        normalView.byteOffset = offset;
        normalView.byteLength = normalSize;
        normalView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        offset += normalSize;

        normalAccessor.bufferView = 1;
        normalAccessor.byteOffset = 0;
        normalAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        normalAccessor.count = vertexCount;
        normalAccessor.type = TINYGLTF_TYPE_VEC3;
        normalAccessorIdx = 1;
    }

    // Colors (RGBA float)
    int colorAccessorIdx = -1;
    if (includeColors && mesh.hasColors()) {
        std::vector<float> colors(vertexCount * 4);
        for (size_t i = 0; i < vertexCount; i++) {
            colors[i * 4 + 0] = mesh.colors[i].r;
            colors[i * 4 + 1] = mesh.colors[i].g;
            colors[i * 4 + 2] = mesh.colors[i].b;
            colors[i * 4 + 3] = 1.0f;  // Alpha
        }
        memcpy(buffer.data.data() + offset, colors.data(), colorSize);

        colorView.buffer = 0;
        colorView.byteOffset = offset;
        colorView.byteLength = colorSize;
        colorView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        offset += colorSize;

        colorAccessor.bufferView = mesh.hasNormals() ? 2 : 1;
        colorAccessor.byteOffset = 0;
        colorAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        colorAccessor.count = vertexCount;
        colorAccessor.type = TINYGLTF_TYPE_VEC4;
        colorAccessorIdx = mesh.hasNormals() ? 2 : 1;
    }

    // Indices
    memcpy(buffer.data.data() + offset, mesh.indices.data(), indexSize);

    indexView.buffer = 0;
    indexView.byteOffset = offset;
    indexView.byteLength = indexSize;
    indexView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

    int indexViewIdx = 0;
    if (mesh.hasNormals()) indexViewIdx++;
    if (includeColors && mesh.hasColors()) indexViewIdx++;

    indexAccessor.bufferView = indexViewIdx + 1;
    indexAccessor.byteOffset = 0;
    indexAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    indexAccessor.count = indexCount;
    indexAccessor.type = TINYGLTF_TYPE_SCALAR;

    // Build model
    model.buffers.push_back(buffer);
    model.bufferViews.push_back(positionView);
    if (mesh.hasNormals()) model.bufferViews.push_back(normalView);
    if (includeColors && mesh.hasColors()) model.bufferViews.push_back(colorView);
    model.bufferViews.push_back(indexView);

    model.accessors.push_back(positionAccessor);
    if (mesh.hasNormals()) model.accessors.push_back(normalAccessor);
    if (includeColors && mesh.hasColors()) model.accessors.push_back(colorAccessor);
    model.accessors.push_back(indexAccessor);

    // Material that uses vertex colors
    tinygltf::Material material;
    material.name = "VertexColorMaterial";
    material.pbrMetallicRoughness.baseColorFactor = {1.0, 1.0, 1.0, 1.0};  // White base, vertex colors multiply
    material.pbrMetallicRoughness.metallicFactor = 0.0;
    material.pbrMetallicRoughness.roughnessFactor = 0.8;
    material.doubleSided = true;
    model.materials.push_back(material);

    // Primitive
    primitive.attributes["POSITION"] = 0;
    if (normalAccessorIdx >= 0) primitive.attributes["NORMAL"] = normalAccessorIdx;
    if (colorAccessorIdx >= 0) primitive.attributes["COLOR_0"] = colorAccessorIdx;
    primitive.indices = static_cast<int>(model.accessors.size() - 1);
    primitive.mode = TINYGLTF_MODE_TRIANGLES;
    primitive.material = 0;  // Use the vertex color material

    gltfMesh.primitives.push_back(primitive);
    model.meshes.push_back(gltfMesh);

    // Node
    tinygltf::Node node;
    node.mesh = 0;
    model.nodes.push_back(node);

    // Scene
    scene.nodes.push_back(0);
    model.scenes.push_back(scene);
    model.defaultScene = 0;

    // Asset info
    model.asset.version = "2.0";
    model.asset.generator = "Marching Cubes Exporter";

    // Write GLB
    tinygltf::TinyGLTF gltf;
    return gltf.WriteGltfSceneToFile(&model, filename, true, true, true, true);
}

// Generate mesh from an SDF function (CPU evaluation)
// Useful for testing without GPU
template<typename SDFFunc>
inline Mesh generateMeshFromFunction(
    SDFFunc sdf,
    int res,
    Vec3 bounds_min,
    Vec3 bounds_max,
    float isolevel = 0.0f
) {
    std::vector<float> distances(res * res * res);

    Vec3 step = {
        (bounds_max.x - bounds_min.x) / (res - 1),
        (bounds_max.y - bounds_min.y) / (res - 1),
        (bounds_max.z - bounds_min.z) / (res - 1)
    };

    int idx = 0;
    for (int z = 0; z < res; z++) {
        for (int y = 0; y < res; y++) {
            for (int x = 0; x < res; x++) {
                Vec3 p = {
                    bounds_min.x + x * step.x,
                    bounds_min.y + y * step.y,
                    bounds_min.z + z * step.z
                };
                distances[idx++] = sdf(p);
            }
        }
    }

    return generateMesh(distances, res, bounds_min, bounds_max, isolevel);
}

} // namespace mc
