// Test for sdf_mesh.hpp wrapper
// Compile: clang++ -std=c++17 -I../vendor/libfive/libfive/include -I/opt/homebrew/include -I/opt/homebrew/opt/eigen/include/eigen3 -L../vendor/libfive/build/libfive/src -lfive sdf_mesh_test.cpp -o sdf_mesh_test
// Run: DYLD_LIBRARY_PATH=../vendor/libfive/build/libfive/src ./sdf_mesh_test

#include <iostream>
#include "sdf_mesh.hpp"

int main() {
    std::cout << "=== SDF Mesh Generation Test ===\n\n";

    // Test 1: Sphere
    {
        std::cout << "1. Sphere (r=1):\n";
        auto sphere_sdf = sdf::sphere(1.0f);
        auto mesh = sdf::generateMesh(sphere_sdf, -2, -2, -2, 2, 2, 2, 0.1f);
        std::cout << "   Vertices: " << sdf::vertexCount(mesh.get()) << "\n";
        std::cout << "   Triangles: " << sdf::triangleCount(mesh.get()) << "\n";
        sdf::exportOBJ("test_sphere.obj", mesh.get());
        mesh->saveSTL("test_sphere.stl");
        std::cout << "   Exported to test_sphere.obj/stl\n\n";
    }

    // Test 2: Box
    {
        std::cout << "2. Box (1x1x1):\n";
        auto box_sdf = sdf::box(0.5f, 0.5f, 0.5f);
        auto mesh = sdf::generateMesh(box_sdf, -2, -2, -2, 2, 2, 2, 0.05f);
        std::cout << "   Vertices: " << sdf::vertexCount(mesh.get()) << "\n";
        std::cout << "   Triangles: " << sdf::triangleCount(mesh.get()) << "\n";
        sdf::exportOBJ("test_box.obj", mesh.get());
        mesh->saveSTL("test_box.stl");
        std::cout << "   Exported to test_box.obj/stl\n\n";
    }

    // Test 3: Torus
    {
        std::cout << "3. Torus (R=0.5, r=0.2):\n";
        auto torus_sdf = sdf::torus(0.5f, 0.2f);
        auto mesh = sdf::generateMesh(torus_sdf, -1, -1, -1, 1, 1, 1, 0.05f);
        std::cout << "   Vertices: " << sdf::vertexCount(mesh.get()) << "\n";
        std::cout << "   Triangles: " << sdf::triangleCount(mesh.get()) << "\n";
        sdf::exportOBJ("test_torus.obj", mesh.get());
        mesh->saveSTL("test_torus.stl");
        std::cout << "   Exported to test_torus.obj/stl\n\n";
    }

    // Test 4: CSG - Union of sphere and box
    {
        std::cout << "4. CSG Union (sphere + box):\n";
        auto sphere_sdf = sdf::sphere(0.6f);
        auto box_sdf = sdf::box(0.4f, 0.4f, 0.4f);
        auto csg = sdf::opUnion(sphere_sdf, box_sdf);
        auto mesh = sdf::generateMesh(csg, -2, -2, -2, 2, 2, 2, 0.05f);
        std::cout << "   Vertices: " << sdf::vertexCount(mesh.get()) << "\n";
        std::cout << "   Triangles: " << sdf::triangleCount(mesh.get()) << "\n";
        sdf::exportOBJ("test_csg_union.obj", mesh.get());
        mesh->saveSTL("test_csg_union.stl");
        std::cout << "   Exported to test_csg_union.obj/stl\n\n";
    }

    // Test 5: CSG - Subtract box from sphere
    {
        std::cout << "5. CSG Subtract (sphere - box):\n";
        auto sphere_sdf = sdf::sphere(0.8f);
        auto box_sdf = sdf::box(0.5f, 0.5f, 0.5f);
        auto csg = sdf::opSubtract(sphere_sdf, box_sdf);
        auto mesh = sdf::generateMesh(csg, -2, -2, -2, 2, 2, 2, 0.05f);
        std::cout << "   Vertices: " << sdf::vertexCount(mesh.get()) << "\n";
        std::cout << "   Triangles: " << sdf::triangleCount(mesh.get()) << "\n";
        sdf::exportOBJ("test_csg_subtract.obj", mesh.get());
        mesh->saveSTL("test_csg_subtract.stl");
        std::cout << "   Exported to test_csg_subtract.obj/stl\n\n";
    }

    // Test 6: Transformed shape
    {
        std::cout << "6. Transformed (rotated + translated torus):\n";
        auto torus_sdf = sdf::torus(0.4f, 0.15f);
        auto rotated = sdf::rotateX(torus_sdf, 0.5f);
        auto transformed = sdf::translate(rotated, 0.3f, 0.2f, 0.0f);
        auto mesh = sdf::generateMesh(transformed, -2, -2, -2, 2, 2, 2, 0.05f);
        std::cout << "   Vertices: " << sdf::vertexCount(mesh.get()) << "\n";
        std::cout << "   Triangles: " << sdf::triangleCount(mesh.get()) << "\n";
        sdf::exportOBJ("test_transformed.obj", mesh.get());
        mesh->saveSTL("test_transformed.stl");
        std::cout << "   Exported to test_transformed.obj/stl\n\n";
    }

    std::cout << "=== All tests complete! ===\n";
    return 0;
}
