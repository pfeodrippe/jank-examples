# Screenshot Migration to jank - Vulkan Handle Types

**Date:** 2025-12-13

## Task
Convert screenshot functionality from C++ to jank orchestration, without using `cpp/raw`.

## Key Challenge: Vulkan Handle Types

Vulkan types like `VkCommandBuffer`, `VkBuffer`, and `VkDeviceMemory` are **typedef pointers** (e.g., `typedef VkCommandBuffer_T* VkCommandBuffer`). This creates problems in jank:

### The Problem

```clojure
;; This creates VkCommandBuffer* (pointer to VkCommandBuffer)
;; which is effectively VkCommandBuffer_T** (double pointer)
(cpp/VkCommandBuffer. vk/VK_NULL_HANDLE)

;; When we pass (cpp/& cmd) to functions expecting VkCommandBuffer*,
;; we're actually passing VkCommandBuffer** which is wrong
```

### Why It Fails

- `cpp/Type.` creates an instance wrapped in a jank-managed container
- Taking its address gives us a pointer to the container, not to the value
- For typedef pointer types, this results in pointer-to-pointer
- Vulkan functions expect pointer-to-handle, not pointer-to-pointer-to-handle

## Solution: C++ Helper Functions

When dealing with Vulkan handle types, keep the handle manipulation in C++ and expose high-level helpers to jank:

### C++ Helpers Added (`sdf_engine.hpp`)

```cpp
// Dispatch compute shader to render frame
inline bool screenshot_dispatch_compute();

// Create staging buffer and copy image, returns handles via output params
inline bool screenshot_create_staging_and_copy(int64_t* bufferOut, int64_t* memoryOut);

// Map memory and return pixel data pointer
inline void* screenshot_map_memory(int64_t memoryHandle);

// Cleanup resources
inline void screenshot_cleanup(int64_t bufferHandle, int64_t memoryHandle);

// Write PNG (accepts void* to avoid jank cast issues)
inline int screenshot_write_png_helper(const char* filepath, const void* pixels,
                                       uint32_t width, uint32_t height);
```

### jank Orchestration (`screenshot.jank`)

```clojure
(defn save-screenshot! [filepath]
  (if (not (sdfx/engine_initialized))
    false
    (do
      ;; Step 1: Dispatch compute shader
      (sdfx/screenshot_dispatch_compute)

      ;; Step 2: Create staging buffer (output params via int64_t*)
      (let* [buffer-ptr (cpp/new cpp/int64_t (cpp/int64_t. 0))
             memory-ptr (cpp/new cpp/int64_t (cpp/int64_t. 0))
             success (sdfx/screenshot_create_staging_and_copy buffer-ptr memory-ptr)]
        (if (not success) false
          (let* [buffer-handle (cpp/* buffer-ptr)
                 memory-handle (cpp/* memory-ptr)
                 pixels (sdfx/screenshot_map_memory memory-handle)
                 width (sdfx/get_swapchain_width)
                 height (sdfx/get_swapchain_height)
                 result (sdfx/screenshot_write_png_helper filepath pixels width height)]
            (sdfx/screenshot_cleanup buffer-handle memory-handle)
            (> result 0)))))))
```

## Other Issues Encountered

### 1. Flag Type Mismatches
Vulkan flag enums need explicit casting:
```clojure
;; BAD - type mismatch
(cpp/= (cpp/.-flags info) vk/VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)

;; GOOD - explicit cast to Flags type
(cpp/= (cpp/.-flags info)
       (cpp/VkCommandBufferUsageFlags. vk/VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
```

### 2. const Pointer Mismatches
`pCommandBuffers` expects `const VkCommandBuffer*` but `cpp/&` gives non-const:
```clojure
;; This fails with const mismatch
(cpp/= (cpp/.-pCommandBuffers submit-info) (cpp/& cmd))

;; Solution: keep the pointer handling in C++
```

### 3. void* Cast Issues
`cpp/cast` from `void*` to other pointer types doesn't work:
```clojure
;; This fails
(cpp/cast (cpp/type "const uint8_t*") pixels)

;; Solution: make C++ function accept void* directly
inline int screenshot_write_png_helper(..., const void* pixelsVoid, ...) {
    const uint8_t* pixels = static_cast<const uint8_t*>(pixelsVoid);
```

## Pattern: Handle-Based Output Parameters

For C++ functions that need to return Vulkan handles to jank:

```cpp
// Use int64_t output parameters
inline bool func(int64_t* handleOut) {
    VkBuffer buffer;
    // ... create buffer ...
    *handleOut = reinterpret_cast<int64_t>(buffer);
    return true;
}
```

```clojure
;; In jank, allocate storage and dereference
(let* [handle-ptr (cpp/new cpp/int64_t (cpp/int64_t. 0))
       success (sdfx/func handle-ptr)
       handle (cpp/* handle-ptr)]
  ;; use handle...
  )
```

## Files Changed

- `vulkan/sdf_engine.hpp`: Added ~230 lines of screenshot helper functions
- `src/vybe/sdf/screenshot.jank`: Created new file (~50 lines)
- `src/vybe/sdf/render.jank`: Updated to use screenshot module
- `src/vybe/sdf/ui.jank`: Updated to use header require instead of cpp/raw

## Commands

```bash
make sdf  # Test - passed!
```

## Results

- Screenshot functionality works correctly
- No `cpp/raw` used in jank code
- C++ handles complex Vulkan type manipulation
- jank orchestrates the high-level flow
- Clean separation of concerns

## Lesson Learned

For complex C/C++ types like Vulkan handles (typedef pointers), the cleanest approach is:
1. Keep handle manipulation in C++
2. Expose simple helpers that accept/return int64_t handles
3. Let jank orchestrate the overall flow
4. Use output parameters for functions that need to return handles
