// SDF to Mesh Generation using libfive
// Wrapper for converting SDFs to triangle meshes with OBJ/STL export
//
// Usage:
//   #include "sdf_mesh.hpp"
//
//   // Create an SDF (sphere example)
//   auto sphere = sdf::sphere(1.0f);
//
//   // Generate mesh
//   auto mesh = sdf::generateMesh(sphere, {-2,-2,-2}, {2,2,2}, 0.1f);
//
//   // Export
//   sdf::exportOBJ("sphere.obj", mesh.get());
//   mesh->saveSTL("sphere.stl");

#pragma once

#include <memory>
#include <fstream>
#include <string>

#include "libfive/tree/tree.hpp"
#include "libfive/render/brep/mesh.hpp"
#include "libfive/render/brep/settings.hpp"
#include "libfive/render/brep/region.hpp"

namespace sdf {

using Tree = libfive::Tree;
using Mesh = libfive::Mesh;
using Region3 = libfive::Region<3>;
using BRepSettings = libfive::BRepSettings;

// ============================================================================
// SDF Primitives (matching GLSL conventions)
// ============================================================================

inline Tree sphere(float r, float cx = 0, float cy = 0, float cz = 0) {
    auto x = Tree::X() - cx;
    auto y = Tree::Y() - cy;
    auto z = Tree::Z() - cz;
    return sqrt(square(x) + square(y) + square(z)) - r;
}

inline Tree box(float sx, float sy, float sz, float cx = 0, float cy = 0, float cz = 0) {
    auto x = Tree::X() - cx;
    auto y = Tree::Y() - cy;
    auto z = Tree::Z() - cz;
    // SDF box: max(|x|-sx, |y|-sy, |z|-sz)
    return max(max(abs(x) - sx, abs(y) - sy), abs(z) - sz);
}

inline Tree roundBox(float sx, float sy, float sz, float r, float cx = 0, float cy = 0, float cz = 0) {
    return box(sx - r, sy - r, sz - r, cx, cy, cz) - r;
}

inline Tree cylinder(float r, float h, float cx = 0, float cy = 0, float cz = 0) {
    auto x = Tree::X() - cx;
    auto y = Tree::Y() - cy;
    auto z = Tree::Z() - cz;
    auto d_xz = sqrt(square(x) + square(z)) - r;
    auto d_y = abs(y) - h;
    return max(d_xz, d_y);
}

inline Tree torus(float R, float r, float cx = 0, float cy = 0, float cz = 0) {
    auto x = Tree::X() - cx;
    auto y = Tree::Y() - cy;
    auto z = Tree::Z() - cz;
    auto q = sqrt(square(x) + square(z)) - R;
    return sqrt(square(q) + square(y)) - r;
}

inline Tree plane(float nx, float ny, float nz, float d) {
    return Tree::X() * nx + Tree::Y() * ny + Tree::Z() * nz + d;
}

// ============================================================================
// Boolean Operations
// ============================================================================

inline Tree opUnion(Tree a, Tree b) {
    return min(a, b);
}

inline Tree opSubtract(Tree a, Tree b) {
    return max(a, -b);
}

inline Tree opIntersect(Tree a, Tree b) {
    return max(a, b);
}

// Smooth union (k = smoothness factor, typically 0.1-0.5)
inline Tree opSmoothUnion(Tree a, Tree b, float k) {
    // Polynomial smooth min
    auto h = max(k - abs(a - b), Tree::var()) * (1.0f / k);
    return min(a, b) - h * h * k * 0.25f;
}

// ============================================================================
// Transforms
// ============================================================================

inline Tree translate(Tree t, float dx, float dy, float dz) {
    return t.remap(Tree::X() - dx, Tree::Y() - dy, Tree::Z() - dz);
}

inline Tree scale(Tree t, float s) {
    return t.remap(Tree::X() / s, Tree::Y() / s, Tree::Z() / s) * s;
}

inline Tree rotateX(Tree t, float angle) {
    float c = cos(angle), s = sin(angle);
    return t.remap(
        Tree::X(),
        Tree::Y() * c + Tree::Z() * s,
        Tree::Z() * c - Tree::Y() * s
    );
}

inline Tree rotateY(Tree t, float angle) {
    float c = cos(angle), s = sin(angle);
    return t.remap(
        Tree::X() * c - Tree::Z() * s,
        Tree::Y(),
        Tree::X() * s + Tree::Z() * c
    );
}

inline Tree rotateZ(Tree t, float angle) {
    float c = cos(angle), s = sin(angle);
    return t.remap(
        Tree::X() * c + Tree::Y() * s,
        Tree::Y() * c - Tree::X() * s,
        Tree::Z()
    );
}

// ============================================================================
// Mesh Generation
// ============================================================================

struct MeshSettings {
    float min_feature = 0.1f;  // Minimum feature size (resolution)
    int workers = 1;           // Number of worker threads (1 = single-threaded)
};

inline std::unique_ptr<Mesh> generateMesh(
    const Tree& sdf,
    const Eigen::Vector3f& boundsMin,
    const Eigen::Vector3f& boundsMax,
    const MeshSettings& settings = MeshSettings{}
) {
    // libfive Region uses double internally
    Region3 region(boundsMin.cast<double>(), boundsMax.cast<double>());

    BRepSettings brep;
    brep.min_feature = settings.min_feature;
    brep.workers = settings.workers;

    return Mesh::render(sdf, region, brep);
}

// Convenience overload with float arrays
inline std::unique_ptr<Mesh> generateMesh(
    const Tree& sdf,
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    float resolution = 0.1f
) {
    MeshSettings settings;
    settings.min_feature = resolution;
    return generateMesh(sdf,
        Eigen::Vector3f(minX, minY, minZ),
        Eigen::Vector3f(maxX, maxY, maxZ),
        settings);
}

// ============================================================================
// Export Functions
// ============================================================================

inline bool exportOBJ(const std::string& filename, const Mesh* mesh) {
    if (!mesh || mesh->branes.empty()) return false;

    std::ofstream file(filename);
    if (!file.is_open()) return false;

    file << "# Generated by libfive SDF mesh generation\n";
    file << "# Vertices: " << mesh->verts.size() << "\n";
    file << "# Triangles: " << mesh->branes.size() << "\n\n";

    // Write vertices
    for (const auto& v : mesh->verts) {
        file << "v " << v.x() << " " << v.y() << " " << v.z() << "\n";
    }

    file << "\n";

    // Write faces (OBJ uses 1-based indexing)
    for (const auto& t : mesh->branes) {
        file << "f " << (t(0) + 1) << " " << (t(1) + 1) << " " << (t(2) + 1) << "\n";
    }

    file.close();
    return true;
}

// STL export is built into libfive::Mesh::saveSTL()

// ============================================================================
// Utility
// ============================================================================

inline size_t vertexCount(const Mesh* mesh) {
    return mesh ? mesh->verts.size() : 0;
}

inline size_t triangleCount(const Mesh* mesh) {
    return mesh ? mesh->branes.size() : 0;
}

} // namespace sdf
