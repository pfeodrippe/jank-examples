# Standalone Build & Scripts Cleanup - Dec 12, 2025

## What Was Done

### 1. Standalone Build Improvements
- Fixed versioned library symlinks (libSDL3.0.dylib, libvulkan.1.dylib, libshaderc_shared.1.dylib)
- Added symlink creation to `bin/run_sdf.sh` for proper library resolution
- Removed `--static` option (had MoltenVK/Vulkan architectural issues)
- Removed `vendor/SDL3/` directory (no longer needed)

### 2. Fixed ODR Violations in vybe/type.jank
Added `inline` keyword to all C++ helper functions in cpp/raw blocks to prevent redefinition errors during AOT compilation:
- vybe_struct_begin, vybe_struct_add_member, vybe_struct_end
- vybe_add_comp, vybe_set_field_*, vybe_get_field_*
- vybe_get_member_count, vybe_get_member_offset, vybe_get_member_size, vybe_get_member_name
- vybe_get_comp_size, vybe_get_comp_ptr, vybe_get_comp_ptr_const
- vybe_set_*_at, vybe_get_*_at
- vybe_alloc_comp, vybe_free_comp, vybe_type_ptr_to_int64

### 3. Moved Run Scripts to bin/
Moved all `run_*.sh` scripts to `bin/` directory:
- run_sdf.sh
- run_integrated.sh
- run_tests.sh
- run_imgui.sh
- run_jolt.sh
- run_imgui_wasm.sh
- run_jolt_wasm.sh
- run_integrated_wasm.sh
- run_debug_test.sh

### 4. Fixed Script Paths
Updated all scripts in `bin/` to:
- Use `cd "$(dirname "$0")/.."` to navigate to project root
- Reference build scripts with `./build_*.sh` prefix

### 5. Added Flecs to run_sdf.sh
- Added `-Ivendor/flecs/distr` include path
- Added flecs.o and flecs_jank_wrapper_native.o to OBJ_FILES
- Added `--obj` flags for standalone mode JIT phase

### 6. Updated Memory Files
Updated `.serena/memories/` files with new paths:
- project_overview.md
- testing-workflow.md
- suggested_commands.md

## Current Issue
Standalone build with `vybe.flecs` dependency fails with:
```
Failed to find symbol: 'jank_load_vybe_flecs'
```

This happens during jank's `load_o` phase - it's trying to load a precompiled .o module for vybe.flecs that doesn't exist. This appears to be a jank AOT compilation limitation when namespaces have transitive dependencies with cpp/raw blocks.

**JIT mode also fails** with the same error when vybe.sdf requires vybe.flecs, suggesting this is a module loading issue rather than AOT-specific.

## Commands
```bash
# JIT mode (dev)
./bin/run_sdf.sh

# Standalone build
./bin/run_sdf.sh --standalone -o SDFViewer

# Run integrated demo
./bin/run_integrated.sh

# Run tests
./bin/run_tests.sh
```

## Next Steps
1. Investigate why jank is calling `load_o` for vybe.flecs instead of `load_jank`
2. May need to report this as a jank bug
3. Test if removing vybe.flecs dependency from vybe.sdf allows standalone to work
