# jank GC Limitations with Large Loops

## Summary

The `write_png_downsampled` function must remain in C++ (`vulkan/sdf_engine.hpp`) because jank's GC cannot handle ~230k loop iterations. The rest of the screenshot pipeline is successfully in jank.

## Key Discovery: jank GC Cannot Handle Large Loops

### The Problem
When iterating ~230,000 times (640×360 pixels) in jank using `loop/recur` or `dotimes` with native type operations (`cpp/aget`, `cpp/=`, etc.), the GC becomes corrupted:

```
EXC_BAD_ACCESS in GC_generic_malloc_many
EXC_BAD_ACCESS in GC_clear_fl_marks
```

### Why It Happens
Each iteration creates GC-managed objects for:
- Array indices (`cpp/size_t.`, `cpp/uint32_t.`)
- Intermediate calculations
- Loop control variables

At ~230k iterations, this overwhelms the garbage collector.

### Approaches Tried That Did NOT Work

1. **Pure jank loop/recur** - Crashes with GC corruption
2. **Pure jank dotimes** - Same crash
3. **cpp/raw with helper functions** - Also crashes with GC corruption AFTER screenshot is saved
4. **Separate header file with helpers** - `invalid object type` error
5. **Helpers in sdf_engine.hpp namespace** - Crashes during parsing

### The Solution That Works

Keep a single C++ helper function in the header file:

```cpp
// In vulkan/sdf_engine.hpp
inline int write_png_downsampled(const char* filepath, const void* pixelsVoid,
                                  uint32_t width, uint32_t height, uint32_t scale) {
    // All pixel processing in C++
}
```

Call from jank:
```clojure
(defn write-png-downsampled [filepath pixels width height scale]
  (sdfx/write_png_downsampled filepath
                              (cpp/unbox (cpp/type "void*") pixels)
                              (cpp/uint32_t. width)
                              (cpp/uint32_t. height)
                              (cpp/uint32_t. scale)))
```

## Other cpp/cast Limitations Found

### Cannot cast void* to uint8_t*
```clojure
;; This doesn't work:
(cpp/cast (cpp/type "const uint8_t*") void-ptr)
```

### Cannot implicitly convert uint8_t* to void*
Both require C++ helper functions, but using them via `cpp/raw` causes GC issues.

## Debugging Process

Used systematic step-by-step approach:
1. Test just let bindings - WORKS
2. Add allocation - WORKS
3. Add outer loop only - WORKS
4. Add nested inner loop - WORKS
5. Add index calculations - WORKS
6. Add pixel copy in loop - CRASHES
7. Test small loops (10, 1000 iterations) - WORKS
8. Test full size (230k iterations) - CRASHES

Key insight: The crash is related to iteration count, not the operations themselves.

## Final State

### `src/vybe/sdf/screenshot.jank`
- All Vulkan command buffer operations in jank
- Memory allocation/mapping in jank
- Pipeline barriers (`barrier-to-general!`, `barrier-to-transfer-src!`) in jank
- `write-png-downsampled` calls single C++ helper

### `vulkan/sdf_engine.hpp`
- Contains `write_png_downsampled` function (~26 lines) - the only C++ needed for screenshot
- Pipeline barriers were successfully moved to jank (no large loops)

## Commands Used

```bash
# Debug with lldb
./bin/run_sdf.sh --lldb

# Test via nREPL
clj-nrepl-eval -p 5557 "(require 'vybe.sdf.screenshot :reload)"
clj-nrepl-eval -p 5557 "(vybe.sdf.screenshot/save-screenshot! \"test.png\")"

# Full test
make sdf
```

## Key Lessons

**cpp/raw for inline C++ can cause GC corruption** - even if the helper function itself works, the GC may become corrupted later. The safest approach for performance-critical loops is to keep them in header files that are included normally, not via `cpp/raw`.

**Code style for jank/C++ interop:**
- Use `let` instead of `let*` - jank's `let` evaluates bindings sequentially
- For simple functions: call side-effects directly in the body (no `_` bindings needed)
- For complex functions with sequential deps: use `_ (do ...)` to group side-effects

```clojure
;; Good - simple function, side-effects in body:
(defn barrier-to-general! [cmd image]
  (let [vk-cmd (cpp/unbox (cpp/type "struct VkCommandBuffer_T*") cmd)
        barrier (cpp/VkImageMemoryBarrier.)]
    (cpp/= (cpp/.-sType barrier) vk/VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER)
    (cpp/= (cpp/.-oldLayout barrier) vk/VK_IMAGE_LAYOUT_UNDEFINED)
    (vk/vkCmdPipelineBarrier vk-cmd ...)))

;; Good - complex function, use _ (do ...) to group side-effects:
(let [cmd (alloc-cmd-buffer)
      _ (do (begin-cmd-buffer! cmd)
            (barrier-to-general! cmd image)
            (dispatch-compute! cmd))
      staging (create-staging-buffer size)
      _ (do (copy-image-to-buffer! cmd ...)
            (end-cmd-buffer! cmd))]
  ...)

;; Bad - avoid let* and multiple _ bindings:
(let* [cmd (alloc-cmd-buffer)
       _ (begin-cmd-buffer! cmd)
       _ (barrier-to-general! cmd image)
       _ (dispatch-compute! cmd)]
  ...)
```
