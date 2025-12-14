# ImGui NewFrame Initialization Fix

## Date: 2025-12-14

## Problem

CI-generated .app crashed in `ImGui_ImplVulkan_NewFrame()`:
```
Assertion failed: (g.IO.BackendPlatformUserData != NULL &&
"Backend not initialized! Did you call ImGui_ImplSDL3_Init()? Did you call ImGui_ImplSDL3_Shutdown()?")
```

This was caused by `imgui_new_frame()` being called before ImGui was initialized.

## Fix

Added initialization check to `imgui_new_frame()` in `vulkan/sdf_engine.hpp`:

**Before:**
```cpp
inline void imgui_new_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}
```

**After:**
```cpp
inline void imgui_new_frame() {
    auto* e = get_engine();
    if (!e || !e->initialized) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}
```

This matches the pattern used in `poll_events()` and other functions that interact with ImGui.

## Second Issue: Missing Blit Shaders

After the ImGui initialization fix, another crash occurred:
```
Failed to open: vulkan_kim/blit.vert.spv
Failed to open: vulkan_kim/blit.frag.spv
Failed to load blit shaders
```

### Root Cause
The CI installs `shaderc` (provides `glslc`) but the script only looked for `glslangValidator`.

### Fix
Updated `bin/run_sdf.sh` shader compilation to support both compilers:
- `glslangValidator` / `glslangValidator4` - Khronos reference compiler (uses `-V` flag)
- `glslc` - Google's shaderc compiler (auto-detects from extension)

## Third Issue: Linux OOM during PCH generation

Linux jank build was getting killed (exit code 143/SIGTERM) during step 402/407 "Generating incremental.pch".

### Root Cause
The PCH (precompiled header) generation is memory-intensive. GitHub ubuntu-24.04 runners have ~7GB RAM, insufficient for this step.

### Fix
Added swap space creation before jank build in `.github/workflows/ci.yml`:
```yaml
- name: Create swap space
  if: steps.cache-jank.outputs.cache-hit != 'true'
  run: |
    sudo fallocate -l 8G /swapfile
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    free -h
```

## CI Status

- **macOS**: Build and Test succeeded
- **Linux**: Testing with swap space fix

## Next Steps

1. Continue polling CI every 3 minutes
2. Monitor Linux build with swap space
