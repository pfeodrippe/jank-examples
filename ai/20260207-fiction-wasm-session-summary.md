# Fiction WASM - Session Summary

**Date:** 2026-02-07

## FIXED: Embedded Files Now Working!

The core issue was that `--embed-file /path@/target` was being passed as a single quoted argument to em++, which caused it to be treated as an unknown argument.

**Root Cause:** In `emscripten-bundle`, the `--em-flag` values were added to the command array as single elements. When bash expanded `"${em_link_cmd[@]}"`, flags like `--embed-file /path` remained as single arguments with the space inside.

**Fix:** Modified `/Users/pfeodrippe/dev/jank/compiler+runtime/bin/emscripten-bundle` (lines 1211-1225) to detect and split `--embed-file`, `--preload-file`, etc. flags into two separate array elements.

**Result:** 
- WASM file grew from 29MB to 35MB (includes fonts, stories, resources)
- Story content now visible via `strings fiction.wasm | grep "manguier"`

## What Was Accomplished

### 1. Fixed jank Compiler Issues
- **re_pattern codegen bug**: Changed codegen to generate `jank::runtime::re_pattern(jank::runtime::make_box("pattern"))` instead of direct constructor
- **Commented out nrepl server**: Not supported in WASM

### 2. Updated to New WebGPU API (Dawn/emdawnwebgpu)
- Changed from deprecated `-sUSE_WEBGPU=1` to `--use-port=emdawnwebgpu`
- Updated fiction_gfx_webgpu.hpp for new Dawn-style C++ API:
  - Async adapter/device request pattern
  - `wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector` type
  - New status enums (`SuccessOptimal`/`SuccessSuboptimal`)

### 3. Created Graphics Abstraction Layer
- `fiction_gfx/fiction_gfx.hpp` - Dispatcher header (`#ifdef FICTION_USE_WEBGPU`)
- `fiction_gfx/fiction_gfx_vulkan.hpp` - Desktop backend (Vulkan+SDL3)
- `fiction_gfx/fiction_gfx_webgpu.hpp` - WASM backend (WebGPU+canvas)

### 4. WASM Build Compiles Successfully
- Story files ARE now embedded in the WASM (verified with `strings`)
- Module loads successfully in Node.js
- All dependencies load (clojure.core, fiction.parser, vybe.flecs, etc.)

## Current Status

**Working:**
- `make fiction-wasm` builds successfully
- WASM loads in Node.js and initializes jank runtime
- Story content is embedded and accessible
- `-main` function is called

**Next Step: Browser Testing**
- The Node.js test shows `Cannot read properties of null (reading 'getContext')` because there's no canvas
- This is expected - WebGPU requires browser environment

## How to Test in Browser

```bash
# Start HTTP server
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm
python3 -m http.server 8888

# Open browser (Chrome/Edge with WebGPU support)
# Navigate to: http://localhost:8888/fiction_canvas.html
```

## The Key Fix (emscripten-bundle)

```bash
# In /Users/pfeodrippe/dev/jank/compiler+runtime/bin/emscripten-bundle
# Lines 1211-1225: Split --embed-file flags into two arguments

# Before: em_link_cmd+=("${em_flag}")  # "--embed-file /path" as one arg
# After:  em_link_cmd+=("--embed-file" "/path")  # Two separate args
```

## Files Modified

| File | Change |
|------|--------|
| `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` | Fixed re_pattern codegen |
| `/Users/pfeodrippe/dev/something/src/fiction.jank` | Commented out nrepl |
| `/Users/pfeodrippe/dev/something/fiction_gfx/fiction_gfx_webgpu.hpp` | WebGPU backend implementation |
| `/Users/pfeodrippe/dev/something/bin/run_fiction_wasm.sh` | WASM build script with emdawnwebgpu |
| `Makefile` | Updated fiction_gfx WASM target |

## Build Output

```
/Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/fiction.wasm (29MB)
/Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/fiction.js (155KB)
/Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/fiction_canvas.html
```

## Potential Issues to Watch

1. **WebGPU rendering not implemented** - The WebGPU backend has TODO stubs for:
   - Font atlas creation and upload
   - Text vertex buffer management
   - Pipeline creation from WGSL shaders
   - Render pass recording

2. **Async initialization** - WebGPU uses async adapter/device request pattern, need to ensure jank code handles this

## Next Steps

1. Test in browser with WebGPU
2. Debug "not a number: nil" error - added defensive color handling
3. Implement actual WebGPU rendering (currently stubs)
4. Create WGSL shaders (text.wgsl, bg.wgsl, particle.wgsl)
5. Test with Chrome DevTools for WebGPU debugging

## Recent Fixes (Session 2)

### Color nil handling (render.jank)
Changed `add-history-entry!` to defensively handle nil color values:
```clojure
;; Before: [r g b] (get-speaker-color speaker-id)
;; After:
(let [color (get-speaker-color speaker-id)
      r (or (nth color 0 nil) 180)
      g (or (nth color 1 nil) 180)
      b (or (nth color 2 nil) 180)
      ...]
```

### Fixed FS debug in HTML
Made `fiction_canvas.html` check if `module.FS` exists before accessing it.
