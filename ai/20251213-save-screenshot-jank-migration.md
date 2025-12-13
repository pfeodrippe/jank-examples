# save_screenshot Migrated to jank

**Date:** 2025-12-13

## Task
Convert `save_screenshot` from C++ to jank orchestration.

## Approach

The original `save_screenshot` was a ~200 line C++ function doing:
1. Dispatch compute shader to render current frame
2. Create Vulkan staging buffer
3. Copy GPU image to staging buffer
4. Map memory, downsample pixels 4x
5. Write PNG with stbi_write_png
6. Cleanup

### Strategy: Handle-based API
Complex Vulkan operations stay in C++, but jank orchestrates the flow.

## C++ Changes (`vulkan/sdf_engine.hpp`)

Added helper functions with handle-based API:

```cpp
// Data structure for screenshot (returned as handle)
struct ScreenshotData {
    uint8_t* rgb_data = nullptr;  // Downsampled RGB pixels
    int32_t width = 0;
    int32_t height = 0;
};

// Capture: does all Vulkan work, returns handle
inline int64_t screenshot_capture();

// Accessors
inline int32_t screenshot_width(int64_t handle);
inline int32_t screenshot_height(int64_t handle);
inline void* screenshot_data(int64_t handle);

// PNG writing (wraps stbi_write_png)
inline int screenshot_write_png(const char* filepath, int64_t handle);

// Cleanup
inline void screenshot_free(int64_t handle);
```

## jank Changes (`src/vybe/sdf/render.jank`)

New `save-screenshot!` function:

```clojure
(defn save-screenshot!
  "Capture screenshot and save to PNG file.
   Uses C++ helpers for Vulkan capture, writes PNG from jank."
  [filepath]
  (let [handle (cpp/sdfx.screenshot_capture)]
    (if (= handle 0)
      (do
        (println "Screenshot capture failed - engine not initialized")
        false)
      (let [w (cpp/sdfx.screenshot_width handle)
            h (cpp/sdfx.screenshot_height handle)
            result (cpp/sdfx.screenshot_write_png filepath handle)]
        (cpp/sdfx.screenshot_free handle)
        (if (> result 0)
          (do
            (println "Screenshot saved to" filepath (str "(" w "x" h ")"))
            true)
          (do
            (println "Failed to write PNG:" filepath)
            false))))))
```

## Key Patterns Used

1. **Handle-based API**: Return `int64_t` handle instead of raw pointer for jank compatibility
2. **Accessor functions**: Separate functions for each piece of data
3. **Explicit cleanup**: `screenshot_free` to avoid memory leaks
4. **Error handling**: Check handle == 0 for failure case

## Commands

```bash
make sdf  # Test - passed!
```

## Results

- Screenshot functionality works exactly as before
- Logic flow now visible in jank code
- C++ still handles complex Vulkan operations
- Clean separation of concerns

## What's Next

More functions could be migrated to jank using similar patterns:
- `init` / `cleanup` orchestration
- Shader switching logic
- Camera controls
