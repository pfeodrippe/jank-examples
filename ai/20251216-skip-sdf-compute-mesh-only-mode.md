# Skip SDF Compute in Mesh-Only Mode

## Date: 2025-12-16

## Problem
When "Show Mesh" was enabled with "Solid Mode" on, the GPU was still using high resources. The SDF raymarching compute shader was running every frame even though the mesh overlay was completely covering the SDF output.

## Root Cause
In `vulkan/sdf_engine.hpp`, the `draw_frame()` function had the `meshOnly` check happening AFTER the compute dispatch:

```cpp
// OLD: Compute always runs when dirty
if (e->dirty) {
    // ... expensive compute shader dispatch ...
}

// meshOnly check happens here, but compute already ran!
bool meshOnly = e->meshPreviewVisible && e->meshRenderSolid && ...;
```

## Solution
Moved the `meshOnly` check BEFORE the compute dispatch and added it as a condition:

```cpp
// NEW: Check meshOnly first
bool meshOnly = e->meshPreviewVisible && e->meshRenderSolid && e->meshPipelineInitialized && e->meshIndexCount > 0;

// Skip compute entirely when showing mesh-only
if (e->dirty && !meshOnly) {
    // ... compute shader dispatch only when needed ...
}
```

## Files Changed
- `vulkan/sdf_engine.hpp:1763-1879` - `draw_frame()` function - skip compute when meshOnly
- `vulkan/sdf_engine.hpp:5855` - Added `is_sdf_compute_skipped()` helper function
- `src/vybe/sdf/ui.jank:48-58` - Added GPU info display in ImGui panel

## GPU Info Added to UI
- **Frame time (ms)**: Shows milliseconds per frame (lower = less GPU load)
- **SDF Compute status**:
  - Green "SKIPPED" when mesh-only mode is active (GPU should be idle)
  - Orange "RUNNING" when SDF raymarching is active

## Commands
- `make sdf` now automatically detects header changes and cleans jank cache

## Makefile Improvement
Proper Make-based build system for SDF viewer:

**Object files now built by Make with dependencies:**
- `vulkan/imgui/*.o` - ImGui (delegates to vulkan/imgui/Makefile)
- `vulkan/stb_impl.o` - STB image implementation
- `vulkan/tinygltf_impl.o` - TinyGLTF (depends on marching_cubes.hpp)
- `vendor/flecs/distr/flecs.o` - Flecs ECS
- `vendor/vybe/vybe_flecs_jank.o` - Jank-Flecs bridge

**Automatic jank cache invalidation:**
- Tracks headers: `sdf_engine.hpp`, `sdf_mesh.hpp`, `marching_cubes.hpp`
- Marker file `.sdf_header_marker` (in project root, survives cache clean)
- When headers change, cleans `target/` automatically

**Usage:**
- `make sdf` - Builds .o files if needed, invalidates cache if headers changed, runs
- Second `make sdf` is instant if nothing changed

## Auto-Rotation Feature Added
- Added "Auto Rotate" checkbox in Mesh Preview section
- When enabled, mesh rotates automatically at constant speed
- Useful for testing mesh-only rendering performance without manual input

## What's Next
- Verify the "SDF Compute: SKIPPED" message appears when Show Mesh + Solid Mode is enabled
- Test with Auto Rotate enabled - FPS should stay high in mesh-only mode
- If GPU is still high when SKIPPED shows, the mesh rendering itself may be expensive (check triangle count)
