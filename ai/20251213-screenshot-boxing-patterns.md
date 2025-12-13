# Screenshot Migration - Boxing Patterns for Vulkan Handles

**Date:** 2025-12-13

## Task
Continued migration of screenshot functionality from C++ to jank, focusing on boxing patterns for Vulkan typedef pointer types.

## Key Learnings

### 1. Vulkan Handle Boxing with `struct` Types

Vulkan types like `VkCommandBuffer` are typedef pointers (`typedef VkCommandBuffer_T* VkCommandBuffer`). When using `cpp/unbox`, jank's `cpp/type "VkCommandBuffer"` adds an extra pointer level, resulting in double-pointers.

**Solution:** Use the underlying struct pointer type:
```clojure
;; Wrong - creates VkCommandBuffer* (double pointer)
(cpp/unbox (cpp/type "VkCommandBuffer") cmd)

;; Correct - creates VkCommandBuffer_T* (correct type)
(cpp/unbox (cpp/type "struct VkCommandBuffer_T*") cmd)
```

This pattern applies to all Vulkan handles:
- `struct VkCommandBuffer_T*` for `VkCommandBuffer`
- `struct VkBuffer_T*` for `VkBuffer`
- `struct VkDeviceMemory_T*` for `VkDeviceMemory`
- `struct VkImage_T*` for `VkImage`

### 2. Function Parameters and Boxing

When native values (enums, flags) pass through jank function parameters, they get boxed. `cpp/unbox` only works for pointer types, not enums.

**Solutions tried:**
- Macros with syntax-quote: Failed due to gensym issues in jank
- Direct casting: `cpp/cast` doesn't work for value types

**Final solution:** Keep complex Vulkan operations (with enums/flags) in C++ helpers.

### 3. Output Parameters Don't Work Well

`cpp/new cpp/VkBuffer` creates `VkBuffer*` which is `VkBuffer_T**`. This adds too many pointer levels for output parameters.

**Solution:** Return structs from C++:
```cpp
struct ScreenshotBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    bool success;
};
inline ScreenshotBuffer create_screenshot_buffer(VkDeviceSize size);
```

```clojure
(let* [result (sdfx/create_screenshot_buffer (cpp/VkDeviceSize. size))]
  (when (cpp/.-success result)
    {:buffer (cpp/box (cpp/.-buffer result))
     :memory (cpp/box (cpp/.-memory result))}))
```

### 4. void* Handling

Functions returning `void*` can't be auto-converted to jank objects.

**Solution:** Box the result:
```clojure
(defn map-memory [memory size]
  (cpp/box (sdfx/map_screenshot_memory ...)))
```

For processing `void*` (like pixel data), keep the processing in C++ to avoid cast issues.

## Final Architecture

**jank orchestrates:**
- High-level flow (allocate, execute, cleanup)
- Boxing/unboxing of handles
- Calling C++ helpers

**C++ helpers handle:**
- Output parameter operations
- Enum/flag operations (pipeline barriers)
- Type casts (void* to uint8_t*)
- Struct/array setup operations

## Files Changed

- `vulkan/sdf_engine.hpp`: Added helpers (~170 lines total):
  - `ScreenshotBuffer` struct
  - `alloc_screenshot_cmd()`, `free_screenshot_cmd()`, `submit_screenshot_cmd()`
  - `create_screenshot_buffer()`, `destroy_screenshot_buffer()`
  - `map_screenshot_memory()`, `unmap_screenshot_memory()`
  - `barrier_to_general()`, `barrier_to_transfer_src()`
  - `dispatch_screenshot_compute()`, `copy_image_to_buffer()`
  - `write_png_downsampled()`

- `src/vybe/sdf/screenshot.jank`: Simplified (~115 lines):
  - Uses boxing pattern with `struct VkXxx_T*` types
  - Orchestrates high-level screenshot flow
  - Delegates complex Vulkan ops to C++ helpers

## Commands

```bash
make sdf  # Build and test - PASSED
```

## Results

- Screenshot functionality works correctly
- Clean separation: jank orchestration, C++ low-level ops
- Pattern established for handling Vulkan handles in jank

## Pattern Summary

```clojure
;; Boxing a Vulkan handle
(cpp/box (sdfx/get_vulkan_handle))

;; Unboxing a Vulkan handle (use struct type)
(cpp/unbox (cpp/type "struct VkXxx_T*") boxed-handle)

;; Boxing void* return values
(cpp/box (sdfx/returns_void_ptr ...))

;; Accessing struct fields from C++ return
(cpp/.-fieldname result)
```
