# Fiction WASM - Forward Declarations Fix

**Date:** 2026-02-07

## Problem

Build was failing with:
```
fiction_gfx_webgpu.hpp:804: error: use of undeclared identifier 'wait_for_device'
fiction_gfx_webgpu.hpp:806: error: use of undeclared identifier 'is_device_ready'
```

## Root Cause

In `fiction_gfx_webgpu.hpp`, the `init()` function (line 737) calls `wait_for_device()` and `is_device_ready()`, but these functions are defined AFTER `init()` (lines 809 and 814 respectively).

## Fix Applied

Added forward declarations around line 680 (after `configure_surface()` ends, before `on_device_ready()`):

```cpp
// Forward declarations for async WebGPU initialization
inline bool is_device_ready();
inline void wait_for_device();
```

## What These Functions Do

1. **`is_device_ready()`** - Returns true when WebGPU device is acquired (async process)
2. **`wait_for_device()`** - Loops with `emscripten_sleep(10)` to yield to browser event loop, allowing async WebGPU callbacks to fire

## Build Result

Build succeeded:
- WASM: 35MB (includes fonts, stories, resources)
- JS loader: 155KB

## What to Test

Open in browser:
```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm
python3 -m http.server 8888
# Open: http://localhost:8888/fiction_canvas.html
```

Expected console output:
```
[fiction-wasm] Canvas size: 1280x720
[fiction-wasm] Requesting WebGPU adapter...
[fiction-wasm] Adapter acquired, requesting device...
[fiction-wasm] Device ready, engine initialized
[fiction-wasm] Surface configured: 1280x720
```

## Files Modified

| File | Change |
|------|--------|
| `/Users/pfeodrippe/dev/something/fiction_gfx/fiction_gfx_webgpu.hpp` | Added forward declarations for `is_device_ready()` and `wait_for_device()` |

## Next Steps

1. Test in Chrome/Edge with WebGPU support
2. If WebGPU initialization works, implement actual rendering:
   - Font atlas creation and upload to GPU texture
   - Text vertex buffer management
   - WGSL shader loading (text.wgsl, bg.wgsl)
   - Render pass recording
3. If "Initializing WebGPU..." still hangs, check:
   - Browser DevTools console for errors
   - Whether adapter/device callbacks are firing
   - ASYNCIFY stack size may need increase
