# CI Mesh Shader Fix - 2024-12-15

## Issue
The standalone .app generated from CI couldn't display the mesh preview, while the local build worked fine.

## Root Cause
The mesh SPIR-V shaders (`mesh.vert.spv` and `mesh.frag.spv`) were:
1. Listed in `.gitignore` (via `*.spv` pattern) - so not tracked in git
2. NOT being compiled during the build process in `bin/run_sdf.sh`

The build script only compiled `blit.vert` and `blit.frag` shaders, not the mesh shaders needed for mesh preview rendering.

In local dev, these files existed because they were compiled at some point. In CI, they were never created, causing `init_mesh_pipeline()` to fail silently when it couldn't load the shaders at `sdf_engine.hpp:3715-3716`:

```cpp
auto vertCode = read_file(e->shaderDir + "/mesh.vert.spv");
auto fragCode = read_file(e->shaderDir + "/mesh.frag.spv");

if (vertCode.empty() || fragCode.empty()) {
    std::cerr << "Failed to load mesh shaders" << std::endl;
    return false;
}
```

## Fix Applied
Added mesh shader compilation to `bin/run_sdf.sh` (lines 691-699):

```bash
# Mesh preview shaders (needed for mesh rendering in standalone builds)
if [ ! -f vulkan_kim/mesh.vert.spv ] || [ vulkan_kim/mesh.vert -nt vulkan_kim/mesh.vert.spv ]; then
    echo "Compiling mesh.vert with $GLSLC..."
    $GLSLC $GLSLC_FLAGS vulkan_kim/mesh.vert -o vulkan_kim/mesh.vert.spv
fi
if [ ! -f vulkan_kim/mesh.frag.spv ] || [ vulkan_kim/mesh.frag -nt vulkan_kim/mesh.frag.spv ]; then
    echo "Compiling mesh.frag with $GLSLC..."
    $GLSLC $GLSLC_FLAGS vulkan_kim/mesh.frag -o vulkan_kim/mesh.frag.spv
fi
```

## Commands Run
```bash
# Verify SPV files are gitignored
git ls-files vulkan_kim/*.spv  # (empty - not tracked)

# Test shader compilation
rm -f vulkan_kim/mesh.vert.spv vulkan_kim/mesh.frag.spv
glslangValidator -V vulkan_kim/mesh.vert -o vulkan_kim/mesh.vert.spv
glslangValidator -V vulkan_kim/mesh.frag -o vulkan_kim/mesh.frag.spv
```

## Files Modified
- `bin/run_sdf.sh` - Added mesh shader compilation

## Key Learnings
- All SPIR-V shaders are gitignored, so any shader used at runtime MUST be compiled during the build
- The mesh preview pipeline requires `mesh.vert.spv` and `mesh.frag.spv` in the shader directory
- When debugging "works locally, fails in CI", check for generated files that aren't in version control

## Next Steps
- Push fix and verify CI build produces working mesh preview
- Consider adding error logging that prints the full path when shader loading fails
