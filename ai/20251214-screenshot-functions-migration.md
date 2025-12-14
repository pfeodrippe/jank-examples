# Screenshot Functions Migration to jank

## Summary

Successfully migrated several C++ screenshot helper functions to pure jank and **removed the C++ versions** from `vulkan/sdf_engine.hpp`. Total: **10 C++ functions removed**. Some functions must remain in C++ due to jank limitations with Vulkan handle types.

## Successfully Moved to jank (C++ removed)

| Removed C++ Function | jank Function | Notes |
|---------------------|---------------|-------|
| `free_screenshot_cmd` | `free-cmd-buffer!` | Uses `vkFreeCommandBuffers` directly |
| `unmap_screenshot_memory` | `unmap-memory!` | Uses `vkUnmapMemory` directly |
| `destroy_screenshot_buffer` | `destroy-staging-buffer!` | Uses `vkDestroyBuffer` and `vkFreeMemory` |
| `submit_screenshot_cmd` | `submit-and-wait!` | Uses `VkSubmitInfo` with `merge*` macro |
| `map_screenshot_memory` | `map-memory` | Uses `cpp/new` for output pointer |
| `imgui_new_frame` | `new-frame!` (ui.jank) | Calls ImGui impl functions directly |
| `imgui_render` | `render!` (ui.jank) | Calls `ImGui::Render` directly |

## Dead Code Removed (no replacement needed)

| Removed C++ Function | Notes |
|---------------------|-------|
| `imgui_render_draw_data` | Was unused - ImGui draw data handled elsewhere |
| `find_memory_type_for_screenshot` | Was unused after refactoring |
| `get_physical_device` | Was unused - no external callers |

## Must Stay in C++

| Function | Reason |
|----------|--------|
| `alloc_screenshot_cmd` | `VkCommandBuffer` is a pointer typedef (`struct VkCommandBuffer_T*`), can't create local variable for output param |
| `create_screenshot_buffer` | `VkBuffer` and `VkDeviceMemory` are pointer typedefs, output params don't work in jank |
| `write_png_downsampled` | GC can't handle ~230k loop iterations |
| `find_memory_type` | Used by Vulkan buffer creation, bitwise operations on native types complex in jank |

## Cannot Be Removed - Core Engine Functions

All remaining functions in `sdf_engine.hpp` are either:
1. **Core engine operations**: `init`, `cleanup`, `poll_events`, `update_uniforms`, `draw_frame`
2. **Event handlers**: `handle_mouse_button`, `handle_mouse_motion`, `handle_scroll`, `handle_key_down`, `process_event`
3. **Shader operations**: `reload_shader`, `check_shader_reload`, `scan_shaders`, `load_shader_by_name`, `switch_shader`, `compile_glsl_to_spirv`
4. **Engine struct accessors**: All `get_*` and `set_*` functions access the internal C++ `Engine` struct
5. **Internal utilities**: `get_engine`, `raycast_gizmo`, `select_object`, `read_file`, `read_text_file`

## Key Learnings

### 1. Vulkan Handle Types are Pointer Typedefs

Vulkan handles like `VkCommandBuffer`, `VkBuffer`, `VkDeviceMemory` are defined as:
```c
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkDeviceMemory_T* VkDeviceMemory;
```

When a Vulkan function has an output parameter like `VkBuffer* pBuffer`, it expects a pointer to a pointer. In jank:
- `(cpp/VkBuffer.)` creates a `VkBuffer*` (pointer to struct pointer)
- `(cpp/& buffer)` then creates `VkBuffer**` (pointer to pointer to pointer) - wrong!

### 2. Creating Output Pointers in jank

For simple pointer types like `void*`, use:
```clojure
(let [data (cpp/new (cpp/type "void*"))]
  (vk/vkMapMemory device memory 0 size 0 data)
  (cpp/* data))  ; dereference to get the void*
```

### 3. Type Casts for Vulkan Structs

When passing pointers to command buffers in structs:
```clojure
(cpp/cast (cpp/type "VkCommandBuffer const*") (cpp/& vk-cmd))
```

### 4. Using `merge*` for Vulkan Structs

The `merge*` macro simplifies struct initialization:
```clojure
(u/merge* info {:sType vk/VK_STRUCTURE_TYPE_SUBMIT_INFO
                :commandBufferCount (cpp/uint32_t. 1)
                :pCommandBuffers cmd-ptr})
```

## Commands Used

```bash
# Test incrementally via nREPL
clj-nrepl-eval -p 5557 "(require 'vybe.sdf.screenshot :reload)"
clj-nrepl-eval -p 5557 "(vybe.sdf.screenshot/save-screenshot! \"test.png\")"

# Full rebuild test
make sdf
```

## Files Changed

- `src/vybe/sdf/screenshot.jank` - Migrated functions to pure jank
- C++ functions in `vulkan/sdf_engine.hpp` remain for cases where jank can't handle the types
