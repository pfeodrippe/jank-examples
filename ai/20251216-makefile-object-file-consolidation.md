# Makefile Object File Consolidation

**Date:** 2025-12-16

## Summary

Moved C/C++ object file builds from inline shell scripts in `run_sdf.sh` to the Makefile with proper dependency tracking. This ensures files are only rebuilt when their sources change.

## What Was Done

### 1. Updated Makefile with Object File Targets

Added proper targets for all C/C++ object files needed for the SDF viewer:

- **ImGui objects** (`vulkan/imgui/*.o`):
  - `imgui.o`, `imgui_draw.o`, `imgui_widgets.o`, `imgui_tables.o`
  - `imgui_impl_sdl3.o`, `imgui_impl_vulkan.o`
  - Dependencies: source `.cpp` files and headers

- **STB implementation** (`vulkan/stb_impl.o`):
  - Depends on: `vulkan/stb_impl.c`, `vulkan/stb_image_write.h`

- **TinyGLTF implementation** (`vulkan/tinygltf_impl.o`):
  - Depends on: `vulkan/tinygltf_impl.cpp`, `vulkan/marching_cubes.hpp`

- **Flecs ECS** (`vendor/flecs/distr/flecs.o`):
  - Depends on: `vendor/flecs/distr/flecs.c`, `vendor/flecs/distr/flecs.h`

- **Vybe Flecs jank helper** (`vendor/vybe/vybe_flecs_jank.o`):
  - Uses jank's clang++ for header compatibility
  - Depends on: `vendor/vybe/vybe_flecs_jank.cpp`, `vendor/vybe/vybe_flecs_jank.h`

### 2. Added Shader Compilation Targets

- Pattern rule for `vulkan_kim/%.spv: vulkan_kim/%`
- Targets: `blit.vert.spv`, `blit.frag.spv`, `mesh.vert.spv`, `mesh.frag.spv`
- Auto-detects `glslangValidator` or `glslc`

### 3. Added Shared Library Target

- `vulkan/libsdf_deps.dylib` (macOS) / `.so` (Linux)
- Depends on all object files
- Only rebuilds when dependencies change

### 4. Updated run_sdf.sh

Replaced 60+ lines of inline build logic with:
```bash
if [ "$STANDALONE" = true ]; then
    make build-sdf-deps-standalone JANK_SRC="$JANK_SRC"
else
    make build-sdf-deps JANK_SRC="$JANK_SRC"
fi
```

## New Makefile Targets

- `make build-sdf-deps` - Build dependencies for JIT mode
- `make build-sdf-deps-standalone` - Build dependencies for standalone mode (includes shared library)
- `make build-shaders` - Compile GLSL shaders to SPIR-V
- `make build-imgui-vulkan` - Build ImGui objects for Vulkan/SDL3

## Benefits

1. **Proper dependency tracking** - Files only rebuild when sources change
2. **Faster incremental builds** - No unnecessary recompilation
3. **Centralized build logic** - All C/C++ builds in one place (Makefile)
4. **Cleaner scripts** - run_sdf.sh is now simpler and delegates builds to make

## Files Changed

- `Makefile` - Added ~100 lines of new targets and variables
- `bin/run_sdf.sh` - Removed inline builds, added make calls

## Additional Fix: Jank Header Dependency Tracking

The jank compiler caches compiled modules in `target/` but doesn't detect changes to C++ headers included by jank files (like `sdf_engine.hpp`).

**Solution:** Added a stamp file mechanism to track header changes:

```makefile
JANK_HEADERS = vulkan/sdf_engine.hpp vulkan/marching_cubes.hpp

.jank-cache-stamp: $(JANK_HEADERS)
	@echo "Header changed, invalidating jank cache..."
	rm -rf target/
	@touch $@

sdf: build-sdf-deps .jank-cache-stamp
	./bin/run_sdf.sh
```

Now when you modify `sdf_engine.hpp` or `marching_cubes.hpp`, `make sdf` will automatically invalidate the jank cache and recompile.

## Next Steps

- Consider removing `vulkan/imgui/Makefile` (now redundant, main Makefile handles imgui builds)
- The jank-compiled object files in `target/` are still handled by the jank compiler itself (cannot be moved to Makefile)
- Add more headers to `JANK_HEADERS` if other C++ headers are included by jank files
