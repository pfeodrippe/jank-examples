# iOS ImGui Crash Fix

**Date:** 2025-12-21

## Problem

The iOS app crashes immediately after initialization with:
```
EXC_BAD_ACCESS (SIGSEGV) KERN_INVALID_ADDRESS at 0x0000000000000028
```

**Stack trace:**
```
ImGui_ImplVulkan_RenderDrawData + 48 (imgui_impl_vulkan.cpp:516)
sdfx::draw_frame() + 1308 (sdf_engine.hpp:1929)
sdf_viewer_main + 784 (sdf_viewer_ios.mm:66)
```

## Root Cause

`ImGui::GetDrawData()` returns NULL or invalid data because:
1. `ImGui_ImplSDL3_NewFrame()` - NOT called
2. `ImGui_ImplVulkan_NewFrame()` - NOT called
3. `ImGui::NewFrame()` - NOT called
4. `ImGui::Render()` - NOT called

The desktop version uses jank to manage ImGui frames via `imgui_new_frame` and `imgui_render` calls. The iOS native version (without jank) bypasses this entirely.

## Solution

Add `imgui_begin_frame()` and `imgui_end_frame()` functions to sdf_engine.hpp, then call them from the iOS render loop.

### Changes to sdf_engine.hpp

Add these functions:
```cpp
inline void imgui_begin_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

inline void imgui_end_frame() {
    ImGui::Render();
}
```

### Changes to sdf_viewer_ios.mm

Update render loop:
```cpp
while (!sdfx::should_close()) {
    sdfx::poll_events_only();
    sdfx::update_uniforms(1.0f / 60.0f);

    sdfx::imgui_begin_frame();  // NEW
    // Any ImGui UI code would go here
    sdfx::imgui_end_frame();    // NEW

    sdfx::draw_frame();
}
```

## Implementation

**COMPLETED** - Fix implemented successfully!

### Changes Made:

1. **sdf_engine.hpp** (lines 1959-1968): Added `imgui_begin_frame()` and `imgui_end_frame()` functions

2. **sdf_viewer_ios.mm** (lines 65-75): Updated render loop to call ImGui frame functions

### Result

App now runs without crashing on iPad Pro 13-inch simulator!

### Minor Warnings (Harmless)

The following warnings appear but don't affect functionality:
- "You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support" - Can be fixed by adding to Info.plist
- "Unbalanced calls to begin/end appearance transitions" - SDL3/UIKit internal warning, harmless
