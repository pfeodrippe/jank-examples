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

## CI Status

- **macOS**: Build and Test succeeded
- **Linux**: jank build in progress (waiting for `#include <any>` fix to complete)

## Next Steps

1. Continue polling CI every 3 minutes
2. Once Linux completes, push the imgui_new_frame fix
3. Monitor next CI run for both platforms passing
