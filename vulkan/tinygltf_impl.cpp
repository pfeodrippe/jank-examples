// tinygltf implementation compilation unit for standalone builds
// This file compiles the tinygltf implementation that's defined in marching_cubes.hpp
// so it can be linked into the standalone executable.

// Define SDF_ENGINE_IMPLEMENTATION to enable TINYGLTF_IMPLEMENTATION
#define SDF_ENGINE_IMPLEMENTATION
#include "marching_cubes.hpp"
